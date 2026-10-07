/**
 * @file    export.c
 * @brief   Export buffer and time-sync mailbox (CLAUDE.md 7.1).
 */
#include "export.h"
#include <string.h>
#include "battery.h"
#include "config.h"
#include "crc.h"
#include "rtc_time.h"
#include "watchdog.h"

#define EXPORT_BATCH    40U     /* records per FRAM read (960 bytes) */

export_buffer_t g_export;
volatile time_sync_mailbox_t g_time_sync_mailbox;

status_t export_refresh(void)
{
    const uint32_t count = storage_count();
    status_t st = STATUS_OK;
    for (uint32_t i = 0U; (i < count) && (st == STATUS_OK); i += EXPORT_BATCH) {
        const uint32_t n = ((count - i) < EXPORT_BATCH) ? (count - i) : EXPORT_BATCH;
        st = storage_read_raw(i, n, &g_export.records[i]);
        watchdog_kick();                /* 32 KB at 400 kHz takes ~1 s */
    }
    if (st != STATUS_OK) {
        return st;
    }

    uint32_t now = 0U;
    (void)rtc_time_now_unix(&now);
    uint16_t mv = 0U;
    (void)battery_read_mv(&mv);

    g_export.magic = 0U;                /* invalid while the header changes */
    g_export.version = EXPORT_VERSION;
    g_export.record_size = LOG_RECORD_SIZE;
    g_export.record_count = count;
    g_export.capacity = LOG_CAPACITY;
    g_export.device_rtc_now = now;
    g_export.lsi_hz = rtc_time_lsi_hz();
    g_export.vdd_mv = mv;
    g_export.fw_version = FW_VERSION;
    g_export.crc32 = crc32_ieee(g_export.records, count * LOG_RECORD_SIZE);
    g_export.generation++;              /* the host polls this */
    g_export.magic = EXPORT_MAGIC;
    return STATUS_OK;
}

bool export_peek_time_request(uint32_t *host_unix)
{
    if (g_time_sync_mailbox.magic != TIME_SYNC_MAGIC) {
        return false;
    }
    *host_unix = g_time_sync_mailbox.host_unix;
    return true;
}

void export_clear_time_request(void)
{
    g_time_sync_mailbox.magic = 0U;
}
