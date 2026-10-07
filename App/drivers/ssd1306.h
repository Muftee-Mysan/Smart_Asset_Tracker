/**
 * @file    ssd1306.h
 * @brief   SSD1306 OLED driver: RAM framebuffer, dirty-page flush, text.
 *
 * Coordinates: x in pixels (0..OLED_WIDTH-1), page = 8-pixel text row
 * (0..OLED_HEIGHT/8-1). Drawing only changes the framebuffer; call
 * ssd1306_flush() to send changed pages.
 */
#ifndef APP_SSD1306_H
#define APP_SSD1306_H

#include <stdbool.h>
#include <stdint.h>
#include "status.h"

/* Detects the address (0x3C, then 0x3D), sends the init sequence and clears
 * the display. */
status_t ssd1306_init(void);

/* 7-bit address found by ssd1306_init(), or 0 if none. */
uint8_t ssd1306_address(void);

/* True if ssd1306_init() found a display. When false, flush/sleep/wake
 * return STATUS_NACK without touching the bus. */
bool ssd1306_is_present(void);

void ssd1306_clear(void);
void ssd1306_clear_page(uint8_t page);

/* Draws text with the 6x8 font. scale 1 = 6x8 per char (one page),
 * scale 2 = 12x16 (two pages). Text past the right edge is clipped.
 * Returns the x position after the last character. */
uint8_t ssd1306_draw_text(uint8_t x, uint8_t page, const char *s, uint8_t scale);

/* Sends all changed pages to the display. */
status_t ssd1306_flush(void);

/* Display off + charge pump off / charge pump on + display on
 * (SSD1306 app note: 8Dh 10h/14h, AEh/AFh). */
status_t ssd1306_sleep(void);
status_t ssd1306_wake(void);

#endif /* APP_SSD1306_H */
