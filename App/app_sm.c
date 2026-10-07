/**
 * @file    app_sm.c
 * @brief   Application state machine (CLAUDE.md 6.3).
 *
 * Phase 3: no low-power modes. ST_SLEEP turns the OLED off and puts the
 * PN532 into PowerDown, but the MCU keeps scanning the keypad. Phase 4 adds
 * STOP mode in ST_SLEEP.
 *
 * Key handling: states react to EV_KEY_DOWN. The key that wakes the device
 * is consumed, except 'D', which is timed for the admin long-press.
 */
#include "app_sm.h"
#include <string.h>
#include "battery.h"
#include "config.h"
#include "export.h"
#include "fmt.h"
#include "fram.h"
#include "i2c_bus.h"
#include "keypad.h"
#include "led.h"
#include "pn532.h"
#include "power.h"
#include "rtc_time.h"
#include "ssd1306.h"
#include "stm32f4xx_hal.h"
#include "storage.h"
#include "students.h"
#include "trace.h"
#include "ui.h"
#include "watchdog.h"

#define BOOT_SCREEN_MS      2000U
#define SET_TIME_DIGITS     10U     /* YYMMDDhhmm */
#define BUF_LEN             12U     /* longest entry (10 digits) + NUL + spare */

typedef enum {
    ST_SLEEP,
    ST_SCAN,
    ST_STUDENT,
    ST_ASSET,
    ST_MSG,
    ST_ADMIN_PIN,
    ST_ADMIN_MENU,
    ST_CLEAR_CONFIRM,
    ST_SET_TIME,
    ST_INFO,
    ST_READ_UID,
    ST_FORMAT_CONFIRM,
    ST_EXPORT,
} state_t;

typedef enum {
    NFC_NONE,       /* no card this attempt (or not yet time to try) */
    NFC_CARD,       /* s_uid / s_uid_len filled */
    NFC_FAILED,     /* NFC_FAIL_LIMIT attempts failed in a row */
} nfc_result_t;

static state_t s_state = ST_SLEEP;
static uint32_t s_entered;          /* tick when the state was entered */
static uint32_t s_last_input;       /* tick of the last key press */

static uint32_t s_msg_ms;           /* ST_MSG duration */
static state_t s_msg_next;          /* ST_MSG next state */

static bool s_d_pending;            /* 'D' pressed in sleep, timing long-press */
static uint32_t s_d_tick;

static bool s_nfc_ready;            /* pn532_init() succeeded */
static bool s_nfc_active;           /* scanning; PowerDown on exit */
static uint32_t s_last_nfc;
static uint8_t s_nfc_fails;

static uint8_t s_uid[PN532_UID_MAX];
static uint8_t s_uid_len;
static const student_t *s_student;

static char s_buf[BUF_LEN];         /* asset ID, PIN or time digits */
static uint8_t s_len;
static bool s_clear_second;
static uint8_t s_bor_level;         /* option-byte BOR level read at boot */

static void enter(state_t next);

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static uint32_t elapsed(uint32_t since)
{
    return HAL_GetTick() - since;
}

static bool is_digit(char k)
{
    return (k >= '0') && (k <= '9');
}

static void buf_clear(void)
{
    s_len = 0U;
    s_buf[0] = '\0';
}

static void buf_push(char c, uint8_t max)
{
    if ((s_len < max) && (s_len + 1U < BUF_LEN)) {
        s_buf[s_len++] = c;
        s_buf[s_len] = '\0';
    }
}

static void buf_pop(void)
{
    if (s_len > 0U) {
        s_buf[--s_len] = '\0';
    }
}

/* Shows whatever was just drawn for `ms`, then enters `next`. */
static void msg_drawn(uint32_t ms, state_t next)
{
    s_msg_ms = ms;
    s_msg_next = next;
    enter(ST_MSG);
}

static void msg(const char *big, const char *small, uint32_t ms, state_t next)
{
    (void)ui_on();
    ui_message(big, small);
    msg_drawn(ms, next);
}

/* ------------------------------------------------------------------------- */
/* NFC                                                                        */
/* ------------------------------------------------------------------------- */

static void nfc_start(void)
{
    if (!s_nfc_ready) {
        s_nfc_ready = (pn532_init(NULL) == STATUS_OK);
    }
    s_nfc_active = true;
    s_nfc_fails = 0U;
    s_last_nfc = HAL_GetTick() - NFC_ATTEMPT_PERIOD_MS;   /* first try now */
}

static void nfc_stop(void)
{
    if (s_nfc_active) {
        s_nfc_active = false;
        if (s_nfc_ready && (pn532_power_down() != STATUS_OK)) {
            s_nfc_ready = false;        /* re-init on next start */
        }
    }
}

/* One duty-cycled attempt every NFC_ATTEMPT_PERIOD_MS: a short
 * InListPassiveTarget, then the RF field off (CLAUDE.md 6.5). */
static nfc_result_t nfc_poll(void)
{
    if (!s_nfc_ready || (elapsed(s_last_nfc) < NFC_ATTEMPT_PERIOD_MS)) {
        return s_nfc_ready ? NFC_NONE : NFC_FAILED;
    }
    s_last_nfc = HAL_GetTick();

    uint8_t len = 0U;
    status_t st = pn532_read_uid(s_uid, &len);
    if (pn532_rf_field(false) != STATUS_OK) {
        st = STATUS_ERROR;
    }
    if (st != STATUS_OK) {
        if (++s_nfc_fails >= NFC_FAIL_LIMIT) {
            s_nfc_ready = false;
            return NFC_FAILED;
        }
        return NFC_NONE;
    }
    s_nfc_fails = 0U;
    s_uid_len = len;
    return (len > 0U) ? NFC_CARD : NFC_NONE;
}

/* ------------------------------------------------------------------------- */
/* Actions                                                                    */
/* ------------------------------------------------------------------------- */

static uint32_t parse_u32(const char *s)
{
    uint32_t v = 0U;
    while (*s != '\0') {
        v = v * 10U + (uint32_t)(*s++ - '0');
    }
    return v;
}

/* Battery check on every wake, before NFC is powered (CLAUDE.md 6.8). */
static void wake(void)
{
    uint16_t mv = 0U;
    const batt_level_t lvl = battery_check(&mv);
    ui_set_low_battery(lvl != BATT_OK);
    (void)ui_on();

    if (lvl == BATT_CRITICAL) {
        msg("Battery", "Charge battery", MSG_LONG_MS, ST_SLEEP);
    } else if (storage_is_full()) {
        msg("Log full", "Export the log", MSG_LONG_MS, ST_SLEEP);
    } else if (lvl == BATT_LOW) {
        msg("Battery", "Battery low", MSG_SHORT_MS, ST_SCAN);
    } else {
        enter(ST_SCAN);
    }
}

static void save_scan(void)
{
    log_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.type = REC_TYPE_SCAN;
    rec.flags = rtc_time_is_valid() ? REC_FLAG_TIME_VALID : 0U;
    rec.uid_len = (s_uid_len <= sizeof(rec.uid)) ? s_uid_len : (uint8_t)sizeof(rec.uid);
    memcpy(rec.uid, s_uid, rec.uid_len);
    uint32_t now = 0U;
    (void)rtc_time_now_unix(&now);
    rec.rtc_time = now;
    rec.value = parse_u32(s_buf);

    uint32_t seq = 0U;
    const status_t st = storage_append(&rec, &seq);
    if (st == STATUS_OK) {
        char b[22];
        fmt_t f;
        fmt_init(&f, b, sizeof(b));
        fmt_str(&f, "Record #");
        fmt_u32(&f, seq);
        if (storage_is_nearly_full()) {
            fmt_str(&f, " Log ");
            fmt_u32(&f, LOG_WARN_PERCENT);
            fmt_char(&f, '%');
        }
        (void)ui_on();
        ui_message("Saved", b);
        msg_drawn(storage_is_nearly_full() ? MSG_LONG_MS : MSG_SHORT_MS, ST_SLEEP);
    } else if (st == STATUS_BUSY) {
        msg("Log full", "Export the log", MSG_LONG_MS, ST_SLEEP);
    } else {
        TRACE(TR_ERROR, st);
        msg("Error", "Save failed", MSG_LONG_MS, ST_SLEEP);
    }
}

/* Any time sync (admin menu or host mailbox, CLAUDE.md 6.9 / 7.1): append a
 * 0xA2 record whose rtc_time is the clock just before it is set, then set
 * the RTC and mark the time valid. *logged = sync record written. */
static status_t time_sync(uint32_t new_unix, bool *logged)
{
    log_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.type = REC_TYPE_SYNC;
    rec.flags = rtc_time_is_valid() ? REC_FLAG_TIME_VALID : 0U;
    uint32_t before = 0U;
    (void)rtc_time_now_unix(&before);
    rec.rtc_time = before;
    rec.value = new_unix;
    *logged = (storage_append(&rec, NULL) == STATUS_OK);

    const status_t st = rtc_time_set_unix(new_unix);
    if (st == STATUS_OK) {
        rtc_time_mark_valid();
    }
    return st;
}

static void show_export(const char *status)
{
    ui_export(g_export.record_count, (uint32_t)&g_export, status);
}

/* Host time sync in export mode (CLAUDE.md 7.1 steps 1-5). */
static void export_poll(void)
{
    uint32_t host_unix = 0U;
    if (!export_peek_time_request(&host_unix)) {
        return;
    }
    bool logged = false;
    const status_t st = time_sync(host_unix, &logged);  /* steps 1-2 */
    export_clear_time_request();                        /* step 3 */
    (void)export_refresh();                             /* step 4 */
    s_last_input = HAL_GetTick();
    if (st != STATUS_OK) {
        show_export("Time sync FAILED");
    } else {
        show_export(logged ? "Time synced" : "Synced, log FAILED");  /* step 5 */
    }
}

/* Sets the RTC from the entered local time and logs a sync record. */
static void set_time(void)
{
    datetime_t dt;
    const char *d = s_buf;
    dt.year = (uint16_t)(2000U + (uint32_t)(d[0] - '0') * 10U + (uint32_t)(d[1] - '0'));
    dt.month = (uint8_t)((d[2] - '0') * 10 + (d[3] - '0'));
    dt.day = (uint8_t)((d[4] - '0') * 10 + (d[5] - '0'));
    dt.hour = (uint8_t)((d[6] - '0') * 10 + (d[7] - '0'));
    dt.minute = (uint8_t)((d[8] - '0') * 10 + (d[9] - '0'));
    dt.second = 0U;

    uint32_t new_unix = 0U;
    if (rtc_time_from_datetime(&dt, TZ_OFFSET_MIN, &new_unix) != STATUS_OK) {
        msg("Invalid", "Check date and time", MSG_SHORT_MS, ST_SET_TIME);
        return;
    }
    bool logged = false;
    if (time_sync(new_unix, &logged) != STATUS_OK) {
        msg("Error", "Set time failed", MSG_LONG_MS, ST_ADMIN_MENU);
        return;
    }
    msg("Time set", logged ? NULL : "Sync log failed", MSG_SHORT_MS, ST_ADMIN_MENU);
}

/* ------------------------------------------------------------------------- */
/* State entry                                                                */
/* ------------------------------------------------------------------------- */

static void enter(state_t next)
{
    if ((next != ST_SCAN) && (next != ST_READ_UID)) {
        nfc_stop();                     /* PowerDown whenever scanning ends */
    }
    if ((s_state == ST_EXPORT) && (next != ST_EXPORT)) {
        power_debug_config();           /* Release: debug bits off again */
    }
    s_state = next;
    s_entered = HAL_GetTick();
    s_last_input = s_entered;

    switch (next) {
    case ST_SLEEP:
        s_d_pending = false;
        (void)ui_off();
        break;
    case ST_SCAN:
        (void)ui_on();
        ui_scan();
        nfc_start();
        break;
    case ST_STUDENT:
        (void)ui_on();
        ui_student(s_student);
        break;
    case ST_ASSET:
        buf_clear();
        ui_asset(s_buf);
        break;
    case ST_MSG:
        break;                          /* screen drawn by msg() */
    case ST_ADMIN_PIN:
        (void)ui_on();
        buf_clear();
        ui_admin_pin(0U);
        break;
    case ST_ADMIN_MENU:
        (void)ui_on();
        ui_admin_menu();
        break;
    case ST_CLEAR_CONFIRM:
        s_clear_second = false;
        ui_clear_confirm(storage_count(), false);
        break;
    case ST_SET_TIME:
        (void)ui_on();
        buf_clear();
        ui_set_time(s_buf);
        break;
    case ST_INFO: {
        uint16_t mv = 0U;
        (void)battery_read_mv(&mv);
        ui_info(storage_count(), LOG_CAPACITY, mv, rtc_time_lsi_hz(),
                power_key_wake_count(), power_stop_count(), power_rtc_wake_count(),
                s_bor_level);
        break;
    }
    case ST_READ_UID:
        ui_read_uid(NULL, 0U, NULL);
        nfc_start();
        break;
    case ST_FORMAT_CONFIRM:
        (void)ui_on();
        ui_message("Log error", "#=format  *=skip");
        break;
    case ST_EXPORT:
        /* Debugger must stay attached: debug bits on, no WFI/STOP here
         * (the main loop busy-waits, see app_sm_in_export()). */
        power_debug_enable();
        show_export((export_refresh() == STATUS_OK) ? "Waiting for PC" : "Read FAILED");
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Event handling per state                                                   */
/* ------------------------------------------------------------------------- */

static void on_sleep(const app_event_t *ev)
{
    if (ev->type == EV_KEY_DOWN) {
        if (ev->key == 'D') {
            s_d_pending = true;         /* decide on release or after 3 s */
            s_d_tick = HAL_GetTick();
        } else {
            wake();
        }
    } else if ((ev->type == EV_KEY_UP) && (ev->key == 'D') && s_d_pending) {
        s_d_pending = false;
        wake();
    } else if ((ev->type == EV_TICK) && s_d_pending &&
               (elapsed(s_d_tick) >= ADMIN_LONGPRESS_MS)) {
        s_d_pending = false;
        uint16_t mv = 0U;
        ui_set_low_battery(battery_check(&mv) != BATT_OK);
        enter(ST_ADMIN_PIN);
    }
    /* EV_RTC_WAKE: stay asleep (IWDG kick in Phase 5). */
}

static void on_scan(const app_event_t *ev)
{
    if ((ev->type == EV_KEY_DOWN) && (ev->key == '*')) {
        enter(ST_SLEEP);
        return;
    }
    if (ev->type != EV_TICK) {
        return;
    }
    if (elapsed(s_entered) >= SCAN_TIMEOUT_MS) {
        enter(ST_SLEEP);
        return;
    }
    switch (nfc_poll()) {
    case NFC_CARD:
        s_student = students_lookup(s_uid, s_uid_len);
        if (s_student != NULL) {
            enter(ST_STUDENT);
        } else {
            ui_unregistered(s_uid, s_uid_len);
            msg_drawn(MSG_LONG_MS, ST_SLEEP);
        }
        break;
    case NFC_FAILED:
        msg("NFC error", "Check the reader", MSG_LONG_MS, ST_SLEEP);
        break;
    default:
        break;
    }
}

static void on_asset(const app_event_t *ev)
{
    if (ev->type == EV_TICK) {
        if (elapsed(s_last_input) >= ASSET_IDLE_MS) {
            enter(ST_SLEEP);
        }
        return;
    }
    if (ev->type != EV_KEY_DOWN) {
        return;
    }
    s_last_input = HAL_GetTick();
    if (is_digit(ev->key)) {
        buf_push(ev->key, ASSET_ID_MAX_DIGITS);
        ui_asset(s_buf);
    } else if (ev->key == '*') {
        if (s_len == 0U) {
            enter(ST_SLEEP);            /* '*' on an empty field cancels */
        } else {
            buf_pop();
            ui_asset(s_buf);
        }
    } else if ((ev->key == '#') && (s_len > 0U)) {
        save_scan();
    }
}

static void on_admin_pin(const app_event_t *ev)
{
    if (ev->type == EV_TICK) {
        if (elapsed(s_last_input) >= PIN_IDLE_MS) {
            enter(ST_SLEEP);
        }
        return;
    }
    if (ev->type != EV_KEY_DOWN) {
        return;
    }
    s_last_input = HAL_GetTick();
    if (is_digit(ev->key)) {
        buf_push(ev->key, ADMIN_PIN_MAX_LEN);
        ui_admin_pin(s_len);
    } else if (ev->key == '*') {
        if (s_len == 0U) {
            enter(ST_SLEEP);
        } else {
            buf_pop();
            ui_admin_pin(s_len);
        }
    } else if (ev->key == '#') {
        if (strcmp(s_buf, ADMIN_PIN) == 0) {
            enter(ST_ADMIN_MENU);
        } else {
            msg("Wrong PIN", NULL, MSG_LONG_MS, ST_SLEEP);
        }
    }
}

static void on_admin_menu(const app_event_t *ev)
{
    if (ev->type != EV_KEY_DOWN) {
        return;
    }
    switch (ev->key) {
    case '1':
        enter(ST_EXPORT);
        break;
    case '2':
        enter(ST_CLEAR_CONFIRM);
        break;
    case '3':
        enter(ST_SET_TIME);
        break;
    case '4':
        enter(ST_INFO);
        break;
    case '5':
        enter(ST_READ_UID);
        break;
    case '*':
        enter(ST_SLEEP);
        break;
    default:
        break;
    }
}

static void on_clear_confirm(const app_event_t *ev)
{
    if (ev->type != EV_KEY_DOWN) {
        return;
    }
    if (ev->key == '*') {
        enter(ST_ADMIN_MENU);
    } else if (ev->key == '#') {
        if (!s_clear_second) {
            s_clear_second = true;
            ui_clear_confirm(storage_count(), true);
        } else if (storage_clear() == STATUS_OK) {
            msg("Cleared", "Log is empty", MSG_SHORT_MS, ST_ADMIN_MENU);
        } else {
            msg("Error", "Clear failed", MSG_LONG_MS, ST_ADMIN_MENU);
        }
    }
}

static void on_set_time(const app_event_t *ev)
{
    if (ev->type != EV_KEY_DOWN) {
        return;
    }
    if (is_digit(ev->key)) {
        buf_push(ev->key, SET_TIME_DIGITS);
        ui_set_time(s_buf);
    } else if (ev->key == '*') {
        if (s_len == 0U) {
            enter(ST_ADMIN_MENU);
        } else {
            buf_pop();
            ui_set_time(s_buf);
        }
    } else if ((ev->key == '#') && (s_len == SET_TIME_DIGITS)) {
        set_time();
    }
}

static void on_read_uid(const app_event_t *ev)
{
    if ((ev->type == EV_KEY_DOWN) && (ev->key == '*')) {
        enter(ST_ADMIN_MENU);
        return;
    }
    if (ev->type != EV_TICK) {
        return;
    }
    switch (nfc_poll()) {
    case NFC_CARD:
        s_last_input = HAL_GetTick();   /* a card counts as activity */
        ui_read_uid(s_uid, s_uid_len, students_lookup(s_uid, s_uid_len));
        break;
    case NFC_FAILED:
        msg("NFC error", "Check the reader", MSG_LONG_MS, ST_ADMIN_MENU);
        break;
    default:
        break;
    }
}

static void on_format_confirm(const app_event_t *ev)
{
    if (ev->type != EV_KEY_DOWN) {
        return;
    }
    if (ev->key == '#') {
        if (storage_format() == STATUS_OK) {
            msg("Formatted", "Log is empty", MSG_SHORT_MS, ST_SLEEP);
        } else {
            msg("Error", "Format failed", MSG_LONG_MS, ST_SLEEP);
        }
    } else if (ev->key == '*') {
        enter(ST_SLEEP);                /* log stays unusable until formatted */
    }
}

/* ------------------------------------------------------------------------- */
/* Public                                                                     */
/* ------------------------------------------------------------------------- */

void app_sm_init(void)
{
    s_bor_level = power_check_bor();            /* may reset once (6.10) */
    watchdog_start();                           /* CLAUDE.md 6.10 */
    (void)rtc_time_calibrate_lsi();             /* 6.9: at boot */
    i2c_bus_init();
    (void)i2c_bus_set_speed(I2C_SPEED_HZ);     /* 400 kHz from here on */
    keypad_awake_config();
    led_init();

    const bool oled = (ssd1306_init() == STATUS_OK);
    (void)fram_init();
    const storage_state_t sst = storage_init();
    s_nfc_ready = (pn532_init(NULL) == STATUS_OK);   /* ends in PowerDown */

    const char *fram_txt = "OK";
    if (sst == STORAGE_FAILED) {
        fram_txt = "ERROR";
    } else if (fram_is_emulated()) {
        fram_txt = "RAM EMU";           /* CLAUDE.md 3.5 */
    } else if (sst == STORAGE_NEEDS_FORMAT) {
        fram_txt = "LOG UNREADABLE";
    }
    if (oled) {
        (void)ui_on();
        uint16_t mv = 0U;
        ui_set_low_battery(battery_check(&mv) != BATT_OK);
        ui_boot(fram_txt, s_nfc_ready ? "OK" : "ERROR", storage_count(), g_student_count);
        (void)ui_flush();
        for (uint32_t t = 0U; t < BOOT_SCREEN_MS; t += 100U) {
            watchdog_kick();                    /* blocking waits > 1 s kick */
            HAL_Delay(100U);
        }
    }

    if (sst == STORAGE_NEEDS_FORMAT) {
        enter(ST_FORMAT_CONFIRM);
    } else if (!s_nfc_ready) {
        msg("NFC error", "Export still works", MSG_LONG_MS, ST_SLEEP);
    } else {
        enter(ST_SLEEP);
    }
    (void)ui_flush();
}

void app_sm_handle(const app_event_t *ev)
{
    /* Generic idle timeout for the admin screens. */
    const bool admin = (s_state == ST_ADMIN_MENU) || (s_state == ST_CLEAR_CONFIRM) ||
                       (s_state == ST_SET_TIME) || (s_state == ST_INFO) ||
                       (s_state == ST_READ_UID) || (s_state == ST_FORMAT_CONFIRM);
    if (admin && (ev->type == EV_KEY_DOWN)) {
        s_last_input = HAL_GetTick();
    }
    if (admin && (ev->type == EV_TICK) && (elapsed(s_last_input) >= ADMIN_IDLE_MS)) {
        enter(ST_SLEEP);
    }

    switch (s_state) {
    case ST_SLEEP:
        on_sleep(ev);
        break;
    case ST_SCAN:
        on_scan(ev);
        break;
    case ST_STUDENT:
        if ((ev->type == EV_TICK) && (elapsed(s_entered) >= STUDENT_SHOW_MS)) {
            enter(ST_ASSET);
        }
        break;
    case ST_ASSET:
        on_asset(ev);
        break;
    case ST_MSG:
        if ((ev->type == EV_TICK) && (elapsed(s_entered) >= s_msg_ms)) {
            enter(s_msg_next);
        }
        break;
    case ST_ADMIN_PIN:
        on_admin_pin(ev);
        break;
    case ST_ADMIN_MENU:
        on_admin_menu(ev);
        break;
    case ST_CLEAR_CONFIRM:
        on_clear_confirm(ev);
        break;
    case ST_SET_TIME:
        on_set_time(ev);
        break;
    case ST_INFO:
        if (ev->type == EV_KEY_DOWN) {
            enter(ST_ADMIN_MENU);
        }
        break;
    case ST_READ_UID:
        on_read_uid(ev);
        break;
    case ST_FORMAT_CONFIRM:
        on_format_confirm(ev);
        break;
    case ST_EXPORT:
        if ((ev->type == EV_KEY_DOWN) && (ev->key == '*')) {
            enter(ST_SLEEP);
        } else if (ev->type == EV_TICK) {
            export_poll();
            if (elapsed(s_last_input) >= EXPORT_TIMEOUT_MS) {
                enter(ST_SLEEP);
            }
        }
        break;
    default:
        enter(ST_SLEEP);
        break;
    }

    if (ev->type == EV_TICK) {
        ui_refresh_clock();
        /* Black Pill LED: lit while awake, so sleep/wake is visible. */
        led_set(ui_is_on());
    }
    (void)ui_flush();
}

bool app_sm_in_export(void)
{
    return s_state == ST_EXPORT;
}

bool app_sm_is_sleeping(void)
{
    return s_state == ST_SLEEP;
}
