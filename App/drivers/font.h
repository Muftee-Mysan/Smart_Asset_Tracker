/**
 * @file    font.h
 * @brief   Bitmap fonts for the OLED.
 */
#ifndef APP_FONT_H
#define APP_FONT_H

#include <stdint.h>

/* 6x8 font: 5 glyph columns + 1 blank column, 8 rows. Each column byte has
 * bit 0 = top row, matching the SSD1306 page layout. */
#define FONT6X8_WIDTH       6U
#define FONT6X8_GLYPH_COLS  5U

/* Returns the 5 column bytes for ASCII 0x20..0x7E; '?' for anything else. */
const uint8_t *font6x8_glyph(char c);

#endif /* APP_FONT_H */
