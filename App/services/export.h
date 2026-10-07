/**
 * @file    export.h
 * @brief   Export buffer and time-sync mailbox for tools/export_log.py
 *          (CLAUDE.md 7.1). Read and written by the host over SWD.
 */
#ifndef APP_EXPORT_H
#define APP_EXPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "status.h"
#include "storage.h"

#define EXPORT_MAGIC        0x54505845UL    /* bytes "EXPT" in memory */
#define EXPORT_VERSION      1U
#define TIME_SYNC_MAGIC     0x454D4954UL    /* bytes "TIME" in memory */

typedef struct __attribute__((packed)) {
    uint32_t magic;          /* EXPORT_MAGIC */
    uint16_t version;        /* EXPORT_VERSION */
    uint16_t record_size;    /* LOG_RECORD_SIZE */
    uint32_t generation;     /* incremented on every refresh */
    uint32_t record_count;
    uint32_t capacity;       /* LOG_CAPACITY */
    uint32_t device_rtc_now; /* Unix seconds UTC at refresh */
    uint32_t lsi_hz;
    uint16_t vdd_mv;
    uint16_t fw_version;
    uint32_t crc32;          /* zlib CRC-32 over records[0..record_count) */
    log_record_t records[LOG_CAPACITY];
} export_buffer_t;

/* Offsets the host script relies on. */
_Static_assert(offsetof(export_buffer_t, generation) == 8U, "export layout");
_Static_assert(offsetof(export_buffer_t, crc32) == 32U, "export layout");
_Static_assert(offsetof(export_buffer_t, records) == 36U, "export layout");

typedef struct {
    uint32_t magic;          /* host writes TIME_SYNC_MAGIC last */
    uint32_t host_unix;      /* host writes this first */
} time_sync_mailbox_t;

extern export_buffer_t g_export;
extern volatile time_sync_mailbox_t g_time_sync_mailbox;

/* Copies the header data and all records from FRAM into g_export,
 * generation + 1. */
status_t export_refresh(void);

/* True (and *host_unix filled) if the host posted a time. */
bool export_peek_time_request(uint32_t *host_unix);

/* Clears the mailbox magic once the request has been handled (7.1 step 3). */
void export_clear_time_request(void);

#endif /* APP_EXPORT_H */
