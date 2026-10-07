/**
 * @file    storage.h
 * @brief   Asset log in FRAM (CLAUDE.md 6.7): A/B headers, append, boot
 *          recovery, clear.
 */
#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"

/* FRAM map (CLAUDE.md 6.7). */
#define LOG_HDR_A_ADDR      0x0000U
#define LOG_HDR_B_ADDR      0x0040U
#define LOG_HDR_SLOT_SIZE   0x40U
#define LOG_RECORDS_ADDR    0x0100U
#define LOG_RECORD_SIZE     24U
#define LOG_CAPACITY        1354U       /* (0x8000 - 0x0100) / 24 */

#define LOG_MAGIC           0x4154524BUL    /* 'ATRK' */
#define LOG_VERSION         1U

#define REC_TYPE_SCAN       0xA1U
#define REC_TYPE_SYNC       0xA2U       /* never 0x00/0xFF: blank FRAM */
#define REC_FLAG_TIME_VALID 0x01U

typedef struct __attribute__((packed)) {
    uint32_t magic;        /* LOG_MAGIC */
    uint16_t version;      /* LOG_VERSION */
    uint16_t record_size;  /* LOG_RECORD_SIZE */
    uint32_t generation;   /* incremented on every header write */
    uint32_t record_count;
    uint32_t next_seq;
    uint32_t reserved[8];
    uint32_t crc32;        /* over all preceding bytes */
} log_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;         /* REC_TYPE_SCAN or REC_TYPE_SYNC */
    uint8_t  flags;        /* bit0 = time_valid */
    uint8_t  uid_len;      /* 4 or 7; 0 for sync records */
    uint8_t  uid[7];
    uint32_t seq;
    uint32_t rtc_time;     /* device RTC, Unix s UTC; sync: value before set */
    uint32_t value;        /* scan: asset ID; sync: the new time */
    uint16_t crc16;        /* CRC-16/CCITT-FALSE over bytes 0..21 */
} log_record_t;

_Static_assert(sizeof(log_header_t) <= LOG_HDR_SLOT_SIZE, "header must fit its slot");
_Static_assert(sizeof(log_record_t) == LOG_RECORD_SIZE, "record must be 24 bytes");
_Static_assert(LOG_RECORDS_ADDR + LOG_CAPACITY * LOG_RECORD_SIZE <= 0x8000U,
               "records must fit in 32 KB");

typedef enum {
    STORAGE_READY,          /* header valid (or freshly created on blank FRAM) */
    STORAGE_NEEDS_FORMAT,   /* no valid header but records present: ask user */
    STORAGE_FAILED,         /* FRAM not readable */
} storage_state_t;

/* Boot: picks the valid header with the higher generation, rolls forward a
 * record written just before a reset, or initialises blank FRAM. */
storage_state_t storage_init(void);
storage_state_t storage_state(void);

/* Writes a fresh empty log (record_count 0). Only after user confirmation
 * when storage_init() returned STORAGE_NEEDS_FORMAT. */
status_t storage_format(void);

/* Fills seq and crc16, writes the record, reads it back, then writes the
 * header to the older copy. STATUS_BUSY = log full. *seq_out may be NULL. */
status_t storage_append(log_record_t *rec, uint32_t *seq_out);

/* record_count = 0; next_seq keeps counting so seq numbers stay unique. */
status_t storage_clear(void);

uint32_t storage_count(void);
uint32_t storage_next_seq(void);
bool storage_is_full(void);
/* True at or above LOG_WARN_PERCENT. */
bool storage_is_nearly_full(void);

/* Reads record `index` (0-based) and checks its CRC. */
status_t storage_read(uint32_t index, log_record_t *rec);

#endif /* APP_STORAGE_H */
