/**
 * @file    bringup.c
 * @brief   Phase 1 hardware bring-up test (APP_MODE_BRINGUP).
 *
 * Boot: I2C scan, OLED "Hello", FRAM pattern test in the scratch area,
 * PN532 firmware version + config, VDD. Then a status screen that shows the
 * last key pressed and the UID of any card tapped.
 */
#include "bringup.h"
#include <string.h>
#include "app_main.h"
#include "battery.h"
#include "config.h"
#include "fmt.h"
#include "fram.h"
#include "i2c_bus.h"
#include "keypad.h"
#include "led.h"
#include "rtc_time.h"
#include "ssd1306.h"
#include "stm32f4xx_hal.h"

#define HELLO_MS        1000U
#define LINE_CHARS      21U     /* 128 px / 6 px */
#define I2C_SCAN_FIRST  0x08U   /* 0x00-0x07 and 0x78-0x7F are reserved */
#define I2C_SCAN_LAST   0x77U
#define UID_SHOW_MAX    8U      /* 4 + 16 hex chars fit on one line */

enum {
    LINE_TITLE = 0,
    LINE_I2C,
    LINE_OLED_FRAM,
    LINE_NFC,
    LINE_VDD,
    LINE_KEY,
    LINE_UID,
    LINE_STATS,
};

bringup_t g_bringup;

/* ------------------------------------------------------------------------- */
/* Display helpers                                                            */
/* ------------------------------------------------------------------------- */

static void show_line(uint8_t page, const char *text)
{
    if (g_bringup.oled_addr == 0U) {
        return;
    }
    ssd1306_clear_page(page);
    (void)ssd1306_draw_text(0U, page, text, 1U);
}

static void flush(void)
{
    if ((g_bringup.oled_addr != 0U) && (ssd1306_flush() != STATUS_OK)) {
        g_bringup.oled_errors++;
        g_bringup.oled_lost++;
        g_bringup.oled_addr = 0U;       /* stop drawing; retried every 5 s */
    }
}

static void render_title(void)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "BRINGUP FW ");
    fmt_u32(&f, FW_VERSION_MAJOR);
    fmt_char(&f, '.');
    fmt_u32(&f, FW_VERSION_MINOR);
    show_line(LINE_TITLE, b);
}

static void render_i2c(void)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "I2C:");
    if (g_bringup.i2c_count == 0U) {
        fmt_str(&f, "none");
    }
    for (uint8_t i = 0U; i < g_bringup.i2c_count; i++) {
        fmt_char(&f, ' ');
        fmt_hex8(&f, g_bringup.i2c_found[i]);
    }
    show_line(LINE_I2C, b);
}

static void render_oled_fram(void)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "OLED:");
    fmt_hex8(&f, g_bringup.oled_addr);
    fmt_str(&f, " FRAM:");
    if (g_bringup.fram_emulated) {
        fmt_str(&f, "RAM EMU");             /* CLAUDE.md 3.5 */
    } else {
        fmt_str(&f, g_bringup.fram_ok ? "OK" : "FAIL");
    }
    show_line(LINE_OLED_FRAM, b);
}

static void render_nfc(void)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    fmt_init(&f, b, sizeof(b));
    if (g_bringup.nfc_ok) {
        fmt_str(&f, "NFC:IC ");
        fmt_hex8(&f, g_bringup.nfc_fw.ic);
        fmt_str(&f, " v");
        fmt_u32(&f, g_bringup.nfc_fw.ver);
        fmt_char(&f, '.');
        fmt_u32(&f, g_bringup.nfc_fw.rev);
    } else {
        fmt_str(&f, "NFC:ERROR");
    }
    show_line(LINE_NFC, b);
}

static void render_vdd(void)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "VDD:");
    fmt_u32(&f, g_bringup.vdd_mv);
    fmt_str(&f, "mV");
    show_line(LINE_VDD, b);
}

static void render_key(void)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "Key:");
    if (g_bringup.last_key != KEY_NONE) {
        fmt_char(&f, g_bringup.last_key);
    } else {
        fmt_char(&f, '-');
    }
    fmt_str(&f, "  n=");
    fmt_u32(&f, g_bringup.key_count);
    show_line(LINE_KEY, b);
}

static void render_uid(void)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "UID:");
    if (g_bringup.uid_len == 0U) {
        fmt_str(&f, "tap a card");
    }
    for (uint8_t i = 0U; (i < g_bringup.uid_len) && (i < UID_SHOW_MAX); i++) {
        fmt_hex8(&f, g_bringup.uid[i]);
    }
    show_line(LINE_UID, b);
}

static void render_stats(void)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "Rd:");
    fmt_u32(&f, g_bringup.card_reads);
    fmt_str(&f, " E:");
    fmt_u32(&f, g_bringup.nfc_errors);
    fmt_str(&f, " I:");
    fmt_u32(&f, g_bringup.i2c_errors);
    fmt_str(&f, " R:");
    fmt_u32(&f, g_bringup.i2c_recoveries);
    show_line(LINE_STATS, b);
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                      */
/* ------------------------------------------------------------------------- */

static void i2c_scan(void)
{
    g_bringup.i2c_count = 0U;
    for (uint8_t a = I2C_SCAN_FIRST; a <= I2C_SCAN_LAST; a++) {
        if (i2c_bus_probe(a) && (g_bringup.i2c_count < BRINGUP_I2C_MAX)) {
            g_bringup.i2c_found[g_bringup.i2c_count++] = a;
        }
    }
}

static uint8_t fram_pattern_ok(uint8_t invert)
{
    uint8_t wr[FRAM_SCRATCH_SIZE];
    uint8_t rd[FRAM_SCRATCH_SIZE];
    for (uint32_t i = 0U; i < FRAM_SCRATCH_SIZE; i++) {
        wr[i] = (uint8_t)((i * 37U + 0x5AU) ^ (invert ? 0xFFU : 0x00U));
    }
    memset(rd, 0, sizeof(rd));
    if (fram_write(FRAM_SCRATCH_ADDR, wr, FRAM_SCRATCH_SIZE) != STATUS_OK) {
        return 0U;
    }
    if (fram_read(FRAM_SCRATCH_ADDR, rd, FRAM_SCRATCH_SIZE) != STATUS_OK) {
        return 0U;
    }
    return (memcmp(wr, rd, sizeof(wr)) == 0) ? 1U : 0U;
}

static uint32_t s_nfc_fail_streak;

static void nfc_init(void)
{
    /* Ends with the PN532 in PowerDown; the first card-read attempt then
     * exercises the wake-up path. */
    const status_t st = pn532_init(&g_bringup.nfc_fw);
    g_bringup.nfc_ok = (st == STATUS_OK) ? 1U : 0U;
    s_nfc_fail_streak = 0U;
    if (st != STATUS_OK) {
        g_bringup.nfc_errors++;
    }
}

static void nfc_attempt(void)
{
    uint8_t uid[PN532_UID_MAX];
    uint8_t len = 0U;
    const uint32_t t0 = HAL_GetTick();
    status_t st = pn532_read_uid(uid, &len);
    if (pn532_rf_field(false) != STATUS_OK) {
        st = STATUS_ERROR;
    }
    g_bringup.nfc_attempt_ms = HAL_GetTick() - t0;
    if (g_bringup.nfc_attempt_ms > g_bringup.nfc_attempt_ms_max) {
        g_bringup.nfc_attempt_ms_max = g_bringup.nfc_attempt_ms;
    }
    if (st != STATUS_OK) {
        g_bringup.nfc_errors++;
        if (++s_nfc_fail_streak >= NFC_FAIL_LIMIT) {
            g_bringup.nfc_ok = 0U;      /* lost: re-init from the slow loop */
            render_nfc();
        }
        render_stats();
        return;
    }
    s_nfc_fail_streak = 0U;
    if (len > 0U) {
        memcpy(g_bringup.uid, uid, len);
        g_bringup.uid_len = len;
        g_bringup.card_reads++;
        render_uid();
        render_stats();
    }
}

static void render_all(void)
{
    render_title();
    render_i2c();
    render_oled_fram();
    render_nfc();
    render_vdd();
    render_key();
    render_uid();
    render_stats();
}

/* Re-detects the OLED after it stopped answering (e.g. unplugged). */
static void oled_retry(void)
{
    if (ssd1306_init() == STATUS_OK) {
        g_bringup.oled_addr = ssd1306_address();
        render_all();
    }
}

static void measure_vdd(void)
{
    uint16_t mv = 0U;
    if (battery_read_mv(&mv) == STATUS_OK) {
        g_bringup.vdd_mv = mv;
    }
    render_vdd();
}

/* ------------------------------------------------------------------------- */
/* Main                                                                       */
/* ------------------------------------------------------------------------- */

void bringup_run(void)
{
    memset(&g_bringup, 0, sizeof(g_bringup));
    i2c_bus_init();
    keypad_awake_config();
    led_init();

    i2c_scan();

    if (ssd1306_init() == STATUS_OK) {
        g_bringup.oled_addr = ssd1306_address();
        (void)ssd1306_draw_text(34U, 3U, "Hello", 2U);  /* 5 x 12 px, centred */
        flush();
        HAL_Delay(HELLO_MS);
        ssd1306_clear();
    }

    if (fram_init() == STATUS_OK) {
        g_bringup.fram_emulated = fram_is_emulated() ? 1U : 0U;
        g_bringup.fram_ok = (fram_pattern_ok(0U) && fram_pattern_ok(1U)) ? 1U : 0U;
    }
    nfc_init();
    measure_vdd();

    render_all();
    flush();

    uint32_t last_tick = HAL_GetTick();
    uint32_t last_nfc = last_tick;
    uint32_t last_vdd = last_tick;
    uint32_t last_led = last_tick;

    for (;;) {
        if (rtc_time_take_wake_flag()) {
            g_app_dbg.rtc_wakes++;
        }

        uint32_t now = HAL_GetTick();
        if ((now - last_tick) >= TICK_PERIOD_MS) {
            /* No catch-up: an NFC attempt can block for tens of ms. */
            last_tick = now;
            g_app_dbg.heartbeat++;
            g_bringup.i2c_errors = i2c_bus_error_count();
            g_bringup.i2c_recoveries = i2c_bus_recovery_count();

            /* LED: slow heartbeat when every check passed, fast blink if
             * the OLED, FRAM or NFC check failed. */
            const bool all_ok = (g_bringup.oled_addr != 0U) && g_bringup.fram_ok &&
                                g_bringup.nfc_ok;
            if ((now - last_led) >= (all_ok ? LED_HEARTBEAT_MS : LED_ERROR_BLINK_MS)) {
                last_led = now;
                led_toggle();
            }
            key_event_t ev;
            if (keypad_tick(&ev) && (ev.type == KEY_EV_DOWN)) {
                g_bringup.last_key = ev.key;
                g_bringup.key_count++;
                if (g_bringup.key_log_len < BRINGUP_KEY_LOG) {
                    g_bringup.key_log[g_bringup.key_log_len++] = ev.key;
                }
                render_key();
            }
        }

        if (g_bringup.nfc_ok && ((now - last_nfc) >= NFC_ATTEMPT_PERIOD_MS)) {
            last_nfc = now;
            nfc_attempt();
        }

        if ((now - last_vdd) >= BRINGUP_VDD_PERIOD_MS) {
            last_vdd = now;
            measure_vdd();
            if (!g_bringup.nfc_ok) {
                nfc_init();                 /* e.g. module plugged in again */
                render_nfc();
            }
            if (g_bringup.oled_addr == 0U) {
                oled_retry();
            }
            render_stats();
        }

        flush();

#if APP_IDLE_USE_WFI
        __WFI();
#endif
    }
}
