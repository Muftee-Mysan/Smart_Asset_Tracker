/**
 * @file    storage.c
 * @brief   Asset log in FRAM (CLAUDE.md 6.7): A/B headers, append, boot
 *          recovery, clear.
 *
 * Power-fail safety: a record is written (and verified) before the header
 * that counts it. The header goes to the copy with the older generation, so
 * the newer copy stays intact if the write is interrupted. At boot the valid
 * copy with the higher generation wins, and a verified record sitting at
 * index record_count with seq == next_seq is rolled forward.
 */
#include "storage.h"
#include <stddef.h>
#include <string.h>
#include "config.h"
#include "crc.h"
#include "fram.h"

#define HDR_CRC_LEN     offsetof(log_header_t, crc32)
#define REC_CRC_LEN     offsetof(log_record_t, crc16)
#define FIRST_SEQ       1U
#define SCAN_BATCH      10U         /* records per read when checking for data */

static log_header_t s_hdr;          /* current header */
static uint8_t s_active;            /* copy holding s_hdr: 0 = A, 1 = B */
static storage_state_t s_state = STORAGE_FAILED;

static uint16_t hdr_addr(uint8_t copy)
{
    return (copy == 0U) ? LOG_HDR_A_ADDR : LOG_HDR_B_ADDR;
}

static uint16_t rec_addr(uint32_t index)
{
    return (uint16_t)(LOG_RECORDS_ADDR + index * LOG_RECORD_SIZE);
}

static bool header_valid(const log_header_t *h)
{
    return (h->magic == LOG_MAGIC) && (h->version == LOG_VERSION) &&
           (h->record_size == LOG_RECORD_SIZE) && (h->record_count <= LOG_CAPACITY) &&
           (h->crc32 == crc32_ieee(h, HDR_CRC_LEN));
}

static bool record_valid(const log_record_t *r)
{
    return ((r->type == REC_TYPE_SCAN) || (r->type == REC_TYPE_SYNC)) &&
           (r->crc16 == crc16_ccitt_false(r, REC_CRC_LEN));
}

/* Writes *h to one header copy and verifies it by reading back. */
static status_t write_header_to(uint8_t copy, log_header_t *h)
{
    h->crc32 = crc32_ieee(h, HDR_CRC_LEN);
    status_t st = fram_write(hdr_addr(copy), (const uint8_t *)h, (uint16_t)sizeof(*h));
    if (st == STATUS_OK) {
        log_header_t rb;
        st = fram_read(hdr_addr(copy), (uint8_t *)&rb, (uint16_t)sizeof(rb));
        if ((st == STATUS_OK) && (memcmp(&rb, h, sizeof(rb)) != 0)) {
            st = STATUS_ERROR;
        }
    }
    return st;
}

/* Commits `next` as the new header: generation + 1, written to the older
 * copy. s_hdr only changes when the write succeeded. */
static status_t commit_header(log_header_t next)
{
    next.generation = s_hdr.generation + 1U;
    const uint8_t target = (uint8_t)(s_active ^ 1U);
    const status_t st = write_header_to(target, &next);
    if (st == STATUS_OK) {
        s_hdr = next;
        s_active = target;
    }
    return st;
}

/* True if any slot in the record area holds a valid record. */
static status_t records_present(bool *present)
{
    log_record_t batch[SCAN_BATCH];
    *present = false;
    for (uint32_t i = 0U; i < LOG_CAPACITY; i += SCAN_BATCH) {
        const uint32_t n = ((LOG_CAPACITY - i) < SCAN_BATCH) ? (LOG_CAPACITY - i) : SCAN_BATCH;
        const status_t st = fram_read(rec_addr(i), (uint8_t *)batch,
                                      (uint16_t)(n * LOG_RECORD_SIZE));
        if (st != STATUS_OK) {
            return st;
        }
        for (uint32_t k = 0U; k < n; k++) {
            if (record_valid(&batch[k])) {
                *present = true;
                return STATUS_OK;
            }
        }
    }
    return STATUS_OK;
}

status_t storage_format(void)
{
    log_header_t h;
    memset(&h, 0, sizeof(h));
    h.magic = LOG_MAGIC;
    h.version = LOG_VERSION;
    h.record_size = LOG_RECORD_SIZE;
    h.generation = 1U;
    h.record_count = 0U;
    h.next_seq = FIRST_SEQ;

    status_t st = write_header_to(0U, &h);
    if (st == STATUS_OK) {
        /* Invalidate copy B so an old header there can never win. */
        log_header_t blank;
        memset(&blank, 0, sizeof(blank));
        st = fram_write(LOG_HDR_B_ADDR, (const uint8_t *)&blank, (uint16_t)sizeof(blank));
    }
    if (st == STATUS_OK) {
        s_hdr = h;
        s_active = 0U;
        s_state = STORAGE_READY;
    }
    return st;
}

storage_state_t storage_init(void)
{
    log_header_t a;
    log_header_t b;
    s_state = STORAGE_FAILED;
    if ((fram_read(LOG_HDR_A_ADDR, (uint8_t *)&a, (uint16_t)sizeof(a)) != STATUS_OK) ||
        (fram_read(LOG_HDR_B_ADDR, (uint8_t *)&b, (uint16_t)sizeof(b)) != STATUS_OK)) {
        return s_state;
    }

    const bool va = header_valid(&a);
    const bool vb = header_valid(&b);
    if (!va && !vb) {
        /* No header: format only if the record area is unused, otherwise
         * the user must confirm (CLAUDE.md 6.7). */
        bool present = false;
        if (records_present(&present) != STATUS_OK) {
            return s_state;
        }
        if (present) {
            s_state = STORAGE_NEEDS_FORMAT;
            return s_state;
        }
        (void)storage_format();
        return s_state;
    }

    if (va && (!vb || (a.generation >= b.generation))) {
        s_hdr = a;
        s_active = 0U;
    } else {
        s_hdr = b;
        s_active = 1U;
    }
    s_state = STORAGE_READY;

    /* Roll forward: record written, header not yet updated. */
    if (s_hdr.record_count < LOG_CAPACITY) {
        log_record_t r;
        if ((fram_read(rec_addr(s_hdr.record_count), (uint8_t *)&r, (uint16_t)sizeof(r)) ==
             STATUS_OK) && record_valid(&r) && (r.seq == s_hdr.next_seq)) {
            log_header_t next = s_hdr;
            next.record_count++;
            next.next_seq++;
            (void)commit_header(next);
        }
    }
    return s_state;
}

storage_state_t storage_state(void)
{
    return s_state;
}

status_t storage_append(log_record_t *rec, uint32_t *seq_out)
{
    if (s_state != STORAGE_READY) {
        return STATUS_ERROR;
    }
    if (s_hdr.record_count >= LOG_CAPACITY) {
        return STATUS_BUSY;
    }

    rec->seq = s_hdr.next_seq;
    rec->crc16 = crc16_ccitt_false(rec, REC_CRC_LEN);
    const uint16_t addr = rec_addr(s_hdr.record_count);

    status_t st = fram_write(addr, (const uint8_t *)rec, (uint16_t)sizeof(*rec));
    if (st == STATUS_OK) {
        log_record_t rb;
        st = fram_read(addr, (uint8_t *)&rb, (uint16_t)sizeof(rb));
        if ((st == STATUS_OK) && (memcmp(&rb, rec, sizeof(rb)) != 0)) {
            st = STATUS_ERROR;
        }
    }
    if (st == STATUS_OK) {
        log_header_t next = s_hdr;
        next.record_count++;
        next.next_seq++;
        st = commit_header(next);
    }
    if ((st == STATUS_OK) && (seq_out != NULL)) {
        *seq_out = rec->seq;
    }
    return st;
}

status_t storage_clear(void)
{
    if (s_state != STORAGE_READY) {
        return STATUS_ERROR;
    }
    log_header_t next = s_hdr;
    next.record_count = 0U;
    return commit_header(next);
}

uint32_t storage_count(void)
{
    return (s_state == STORAGE_READY) ? s_hdr.record_count : 0U;
}

uint32_t storage_next_seq(void)
{
    return s_hdr.next_seq;
}

bool storage_is_full(void)
{
    return (s_state == STORAGE_READY) && (s_hdr.record_count >= LOG_CAPACITY);
}

bool storage_is_nearly_full(void)
{
    return (s_state == STORAGE_READY) &&
           ((s_hdr.record_count * 100U) >= (LOG_CAPACITY * LOG_WARN_PERCENT));
}

status_t storage_read_raw(uint32_t index, uint32_t n, log_record_t *out)
{
    if ((s_state != STORAGE_READY) || (index + n > s_hdr.record_count)) {
        return STATUS_INVALID;
    }
    return fram_read(rec_addr(index), (uint8_t *)out, (uint16_t)(n * LOG_RECORD_SIZE));
}

status_t storage_read(uint32_t index, log_record_t *rec)
{
    if ((s_state != STORAGE_READY) || (index >= s_hdr.record_count)) {
        return STATUS_INVALID;
    }
    status_t st = fram_read(rec_addr(index), (uint8_t *)rec, (uint16_t)sizeof(*rec));
    if ((st == STATUS_OK) && !record_valid(rec)) {
        st = STATUS_INVALID;
    }
    return st;
}
