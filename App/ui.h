/**
 * @file    ui.h
 * @brief   Screen rendering, one function per screen (CLAUDE.md 6.3, 6.6).
 *
 * Every screen has a status bar on the top line: a short title on the left,
 * a low-battery mark and the local time on the right. Style: dark
 * background, little lit text, no filled areas. Large text is the 6x8 font
 * at 2x (12x16 px, 10 characters per line).
 * Functions only draw into the framebuffer; ui_flush() sends it.
 */
#ifndef APP_UI_H
#define APP_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"
#include "students.h"

/* Display on/off (OLED charge pump off when off). */
status_t ui_on(void);
status_t ui_off(void);
bool ui_is_on(void);

/* Sends changed pages to the display. */
status_t ui_flush(void);

/* Low-battery mark in the status bar on every screen. */
void ui_set_low_battery(bool low);

/* Redraws the status bar if the displayed minute changed. Call often. */
void ui_refresh_clock(void);

/* Boot summary: FW version, FRAM / NFC status, log and student counts. */
void ui_boot(const char *fram, const char *nfc, uint32_t count, uint32_t students);

void ui_scan(void);
void ui_student(const student_t *s);
void ui_unregistered(const uint8_t *uid, uint8_t uid_len);
void ui_asset(const char *digits);

/* Message screen: `big` at 2x (<= 10 chars), `small` below (may be NULL). */
void ui_message(const char *big, const char *small);

void ui_admin_pin(uint8_t entered);
void ui_admin_menu(void);
void ui_clear_confirm(uint32_t count, bool second_press);
void ui_set_time(const char *digits);
void ui_info(uint32_t count, uint32_t capacity, uint16_t vdd_mv, uint32_t lsi_hz);
void ui_read_uid(const uint8_t *uid, uint8_t uid_len, const student_t *s);

#endif /* APP_UI_H */
