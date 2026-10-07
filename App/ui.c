/**
 * @file    ui.c
 * @brief   Screen rendering, one function per screen (CLAUDE.md 6.3, 6.6).
 */
#include "ui.h"
#include <string.h>
#include "config.h"
#include "fmt.h"
#include "rtc_time.h"
#include "ssd1306.h"

#define CHAR_W          6U          /* 6x8 font cell */
#define LINE_CHARS      21U
#define BIG_CHARS       10U         /* at 2x */
#define PAGE_STATUS     0U
#define PAGE_LAST       ((OLED_HEIGHT / 8U) - 1U)
#define TIME_INVALID_MARK '?'

static bool s_on;
static bool s_low_batt;
static const char *s_title = "";
static uint32_t s_shown_minute = 0xFFFFFFFFUL;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static uint8_t centre_x(const char *s, uint8_t scale)
{
    const uint32_t w = (uint32_t)strlen(s) * CHAR_W * scale;
    return (w >= OLED_WIDTH) ? 0U : (uint8_t)((OLED_WIDTH - w) / 2U);
}

static void text(uint8_t page, const char *s)
{
    (void)ssd1306_draw_text(0U, page, s, 1U);
}

static void text_centre(uint8_t page, const char *s, uint8_t scale)
{
    (void)ssd1306_draw_text(centre_x(s, scale), page, s, scale);
}

static void fmt_uid(fmt_t *f, const uint8_t *uid, uint8_t len)
{
    for (uint8_t i = 0U; i < len; i++) {
        fmt_hex8(f, uid[i]);
    }
}

static void fmt_2d(fmt_t *f, uint32_t v)
{
    fmt_char(f, (char)('0' + (v / 10U) % 10U));
    fmt_char(f, (char)('0' + v % 10U));
}

static void draw_status(void)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    ssd1306_clear_page(PAGE_STATUS);
    text(PAGE_STATUS, s_title);

    fmt_init(&f, b, sizeof(b));
    if (s_low_batt) {
        fmt_str(&f, "BAT! ");
    }
    uint32_t now = 0U;
    if (rtc_time_now_unix(&now) == STATUS_OK) {
        datetime_t dt;
        rtc_time_to_datetime(now, TZ_OFFSET_MIN, &dt);
        fmt_2d(&f, dt.hour);
        fmt_char(&f, ':');
        fmt_2d(&f, dt.minute);
        if (!rtc_time_is_valid()) {
            fmt_char(&f, TIME_INVALID_MARK);
        }
        s_shown_minute = now / 60U;
    }
    (void)ssd1306_draw_text((uint8_t)(OLED_WIDTH - f.len * CHAR_W), PAGE_STATUS, b, 1U);
}

/* Clears the screen and draws the status bar with `title`. */
static void screen(const char *title)
{
    s_title = title;
    ssd1306_clear();
    draw_status();
}

/* ------------------------------------------------------------------------- */
/* Display control                                                            */
/* ------------------------------------------------------------------------- */

status_t ui_on(void)
{
    if (s_on) {
        return STATUS_OK;
    }
    s_on = true;
    return ssd1306_wake();
}

status_t ui_off(void)
{
    s_on = false;
    ssd1306_clear();
    (void)ssd1306_flush();              /* blank RAM for the next wake */
    return ssd1306_sleep();
}

bool ui_is_on(void)
{
    return s_on;
}

status_t ui_flush(void)
{
    return s_on ? ssd1306_flush() : STATUS_OK;
}

void ui_set_low_battery(bool low)
{
    if (low != s_low_batt) {
        s_low_batt = low;
        if (s_on) {
            draw_status();
        }
    }
}

void ui_refresh_clock(void)
{
    uint32_t now = 0U;
    if (s_on && (rtc_time_now_unix(&now) == STATUS_OK) && ((now / 60U) != s_shown_minute)) {
        draw_status();
    }
}

/* ------------------------------------------------------------------------- */
/* Screens                                                                    */
/* ------------------------------------------------------------------------- */

void ui_boot(const char *fram, const char *nfc, uint32_t count, uint32_t students)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    screen("BOOT");
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "Asset tracker FW ");
    fmt_u32(&f, FW_VERSION_MAJOR);
    fmt_char(&f, '.');
    fmt_u32(&f, FW_VERSION_MINOR);
    text(1U, b);
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "FRAM: ");
    fmt_str(&f, fram);
    text(3U, b);
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "NFC: ");
    fmt_str(&f, nfc);
    text(4U, b);
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "Log: ");
    fmt_u32(&f, count);
    fmt_str(&f, " records");
    text(5U, b);
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "Students: ");
    fmt_u32(&f, students);
    text(6U, b);
}

void ui_scan(void)
{
    screen("SCAN");
    text_centre(2U, "Tap card", 2U);
    text_centre(5U, "Tap student card", 1U);
    text(PAGE_LAST, "*=cancel");
}

void ui_student(const student_t *s)
{
    screen("STUDENT");
    text_centre(2U, s->name, 1U);
    text_centre(4U, s->student_id, 2U);
}

void ui_unregistered(const uint8_t *uid, uint8_t uid_len)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    screen("SCAN");
    text_centre(2U, "Unregistered", 1U);
    text_centre(3U, "card", 1U);
    fmt_init(&f, b, sizeof(b));
    fmt_uid(&f, uid, uid_len);
    text_centre(5U, b, 1U);
}

void ui_asset(const char *digits)
{
    char b[BIG_CHARS + 1U];
    fmt_t f;
    screen("ASSET");
    text(2U, "Asset ID:");
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, digits);
    if (strlen(digits) < ASSET_ID_MAX_DIGITS) {
        fmt_char(&f, '_');              /* cursor */
    }
    (void)ssd1306_draw_text(0U, 4U, b, 2U);
    text(PAGE_LAST, "#=save *=delete");
}

void ui_message(const char *big, const char *small)
{
    screen("");
    text_centre(2U, big, 2U);
    if (small != NULL) {
        text_centre(5U, small, 1U);
    }
}

void ui_admin_pin(uint8_t entered)
{
    char b[ADMIN_PIN_MAX_LEN + 2U];
    fmt_t f;
    screen("ADMIN");
    text_centre(2U, "Admin PIN", 1U);
    fmt_init(&f, b, sizeof(b));
    for (uint8_t i = 0U; i < entered; i++) {
        fmt_char(&f, '*');
    }
    if (entered < ADMIN_PIN_MAX_LEN) {
        fmt_char(&f, '_');
    }
    text_centre(4U, b, 2U);
    text(PAGE_LAST, "#=ok *=delete");
}

void ui_admin_menu(void)
{
    screen("ADMIN");
    text(1U, "1 Export");
    text(2U, "2 Clear log");
    text(3U, "3 Set time");
    text(4U, "4 Info");
    text(5U, "5 Read UID");
    text(PAGE_LAST, "*=exit");
}

void ui_clear_confirm(uint32_t count, bool second_press)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    screen("CLEAR");
    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "Clear ");
    fmt_u32(&f, count);
    fmt_str(&f, " records?");
    text_centre(2U, b, 1U);
    text_centre(4U, second_press ? "Press # again" : "#=yes", 1U);
    text(PAGE_LAST, "*=back");
}

void ui_set_time(const char *digits)
{
    /* Template YY-MM-DD hh:mm (local time); entered digits replace '_'. */
    static const char tmpl[] = "YY-MM-DD hh:mm";
    char b[sizeof(tmpl)];
    size_t k = 0U;
    const size_t n = strlen(digits);
    for (size_t i = 0U; i < sizeof(tmpl); i++) {
        const char c = tmpl[i];
        if ((c == '-') || (c == ':') || (c == ' ') || (c == '\0')) {
            b[i] = c;
        } else {
            b[i] = (k < n) ? digits[k] : '_';
            k++;
        }
    }
    screen("SET TIME");
    text_centre(2U, "Enter local time", 1U);
    text_centre(4U, b, 1U);
    text(6U, "YYMMDDhhmm");
    text(PAGE_LAST, "#=set *=delete");
}

void ui_info(uint32_t count, uint32_t capacity, uint16_t vdd_mv, uint32_t lsi_hz,
             uint32_t key_wakes, uint32_t stops, uint32_t rtc_wakes)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    screen("INFO");

    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "Records ");
    fmt_u32(&f, count);
    fmt_char(&f, '/');
    fmt_u32(&f, capacity);
    text(1U, b);

    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "VDD ");
    fmt_u32(&f, vdd_mv);
    fmt_str(&f, " mV");
    text(2U, b);

    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "LSI ");
    if (lsi_hz == 0U) {
        fmt_str(&f, "not measured");
    } else {
        fmt_u32(&f, lsi_hz);
        fmt_str(&f, " Hz");
    }
    text(3U, b);

    uint32_t now = 0U;
    fmt_init(&f, b, sizeof(b));
    if (rtc_time_now_unix(&now) == STATUS_OK) {
        datetime_t dt;
        rtc_time_to_datetime(now, TZ_OFFSET_MIN, &dt);
        fmt_u32(&f, dt.year);
        fmt_char(&f, '-');
        fmt_2d(&f, dt.month);
        fmt_char(&f, '-');
        fmt_2d(&f, dt.day);
        fmt_char(&f, ' ');
        fmt_2d(&f, dt.hour);
        fmt_char(&f, ':');
        fmt_2d(&f, dt.minute);
        fmt_str(&f, rtc_time_is_valid() ? "" : " ?");
    }
    text(4U, b);

    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "FW ");
    fmt_u32(&f, FW_VERSION_MAJOR);
    fmt_char(&f, '.');
    fmt_u32(&f, FW_VERSION_MINOR);
    text(5U, b);

    fmt_init(&f, b, sizeof(b));
    fmt_str(&f, "W");
    fmt_u32(&f, key_wakes);
    fmt_str(&f, " S");
    fmt_u32(&f, stops);
    fmt_str(&f, " R");
    fmt_u32(&f, rtc_wakes);
    text(6U, b);
    text(PAGE_LAST, "Any key=back");
}

void ui_read_uid(const uint8_t *uid, uint8_t uid_len, const student_t *s)
{
    char b[LINE_CHARS + 1U];
    fmt_t f;
    screen("READ UID");
    if (uid_len == 0U) {
        text_centre(3U, "Tap any card", 1U);
    } else {
        fmt_init(&f, b, sizeof(b));
        fmt_uid(&f, uid, uid_len);
        text_centre(2U, b, 1U);
        text_centre(4U, (s != NULL) ? s->name : "(not registered)", 1U);
    }
    text(PAGE_LAST, "*=back");
}
