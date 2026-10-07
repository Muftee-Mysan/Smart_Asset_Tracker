/**
 * @file    ssd1306.c
 * @brief   SSD1306 OLED driver: RAM framebuffer, dirty-page flush, text.
 *
 * Command values: SSD1306 datasheet Rev 1.1, section 9 command tables and
 * section 10, plus the charge-pump app note (Rev 0.4) Figure 2 init flow.
 */
#include "ssd1306.h"
#include <string.h>
#include "config.h"
#include "font.h"
#include "i2c_bus.h"

#define OLED_PAGES          (OLED_HEIGHT / 8U)

/* I2C control byte (datasheet 8.1.5.2): Co = 0, D/C# selects what follows. */
#define CTRL_CMD_STREAM     0x00U
#define CTRL_DATA_STREAM    0x40U

/* Commands (datasheet Table 9-1 and app note). */
#define CMD_DISPLAY_OFF     0xAEU
#define CMD_DISPLAY_ON      0xAFU
#define CMD_SET_CLOCK       0xD5U   /* A[7:4] Fosc, A[3:0] divide-1 */
#define CMD_SET_MUX         0xA8U   /* MUX ratio = A + 1 */
#define CMD_SET_OFFSET      0xD3U
#define CMD_START_LINE_0    0x40U
#define CMD_CHARGE_PUMP     0x8DU   /* app note: 14h = on, 10h = off */
#define CMD_ADDR_MODE       0x20U   /* 02h = page addressing (reset value) */
#define CMD_SEG_REMAP_ON    0xA1U   /* column 127 -> SEG0 */
#define CMD_SEG_REMAP_OFF   0xA0U
#define CMD_COM_SCAN_DEC    0xC8U   /* scan COM[N-1] -> COM0 */
#define CMD_COM_SCAN_INC    0xC0U
#define CMD_COM_PINS        0xDAU   /* 12h for 128x64 (note 1, Table 10-3), 02h for 128x32 */
#define CMD_CONTRAST        0x81U
#define CMD_PRECHARGE       0xD9U   /* reset value 22h */
#define CMD_VCOMH           0xDBU   /* 20h = 0.77 x VCC (reset value) */
#define CMD_RESUME_RAM      0xA4U   /* display follows RAM */
#define CMD_NORMAL          0xA6U   /* not inverted */
#define CMD_SCROLL_OFF      0x2EU
#define CMD_PAGE_ADDR(p)    (uint8_t)(0xB0U | (p))
#define CMD_COL_LOW(c)      (uint8_t)(0x00U | ((c) & 0x0FU))
#define CMD_COL_HIGH(c)     (uint8_t)(0x10U | ((c) >> 4))

#if OLED_HEIGHT == 64
#define COM_PINS_CFG        0x12U
#elif OLED_HEIGHT == 32
#define COM_PINS_CFG        0x02U
#else
#error "OLED_HEIGHT must be 64 or 32"
#endif

static uint8_t s_fb[OLED_PAGES][OLED_WIDTH];
static uint8_t s_dirty;             /* bit n = page n changed */
static uint8_t s_addr;
static uint8_t s_tx[1U + OLED_WIDTH];

_Static_assert(OLED_PAGES <= 8U, "dirty mask is 8 bits");

static status_t send_cmds(const uint8_t *cmds, uint8_t n)
{
    uint8_t buf[1U + 32U];
    if (s_addr == 0U) {
        return STATUS_NACK;     /* no display: never send to address 0 (general call) */
    }
    if (n > 32U) {
        return STATUS_INVALID;
    }
    buf[0] = CTRL_CMD_STREAM;
    memcpy(&buf[1], cmds, n);
    return i2c_bus_write(s_addr, buf, (uint16_t)(n + 1U));
}

status_t ssd1306_init(void)
{
    if (i2c_bus_probe(I2C_ADDR_OLED_PRIMARY)) {
        s_addr = I2C_ADDR_OLED_PRIMARY;
    } else if (i2c_bus_probe(I2C_ADDR_OLED_ALT)) {
        s_addr = I2C_ADDR_OLED_ALT;
    } else {
        s_addr = 0U;
        return STATUS_NACK;
    }

    static const uint8_t init_seq[] = {
        CMD_DISPLAY_OFF,
        CMD_SET_CLOCK, 0x80U,               /* reset Fosc, divide 1 */
        CMD_SET_MUX, (uint8_t)(OLED_HEIGHT - 1U),
        CMD_SET_OFFSET, 0x00U,
        CMD_START_LINE_0,
        CMD_CHARGE_PUMP, 0x14U,             /* internal charge pump on */
        CMD_ADDR_MODE, 0x02U,               /* page addressing */
#if OLED_FLIP
        CMD_SEG_REMAP_OFF, CMD_COM_SCAN_INC,
#else
        CMD_SEG_REMAP_ON, CMD_COM_SCAN_DEC,
#endif
        CMD_COM_PINS, COM_PINS_CFG,
        CMD_CONTRAST, OLED_CONTRAST,
        CMD_PRECHARGE, 0x22U,
        CMD_VCOMH, 0x20U,
        CMD_RESUME_RAM,
        CMD_NORMAL,
        CMD_SCROLL_OFF,
    };
    status_t st = send_cmds(init_seq, (uint8_t)sizeof(init_seq));
    if (st != STATUS_OK) {
        return st;
    }

    ssd1306_clear();
    st = ssd1306_flush();
    if (st != STATUS_OK) {
        return st;
    }

    /* Display on only after RAM is cleared, so no random pixels flash. */
    const uint8_t on = CMD_DISPLAY_ON;
    return send_cmds(&on, 1U);
}

uint8_t ssd1306_address(void)
{
    return s_addr;
}

bool ssd1306_is_present(void)
{
    return s_addr != 0U;
}

void ssd1306_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
    s_dirty = (uint8_t)((1U << OLED_PAGES) - 1U);
}

void ssd1306_clear_page(uint8_t page)
{
    if (page < OLED_PAGES) {
        memset(s_fb[page], 0, OLED_WIDTH);
        s_dirty |= (uint8_t)(1U << page);
    }
}

/* Spreads the 4 bits of a nibble to 8 bits (each bit doubled vertically). */
static uint8_t double_bits(uint8_t nibble)
{
    uint8_t out = 0U;
    for (uint8_t i = 0U; i < 4U; i++) {
        if ((nibble & (1U << i)) != 0U) {
            out |= (uint8_t)(3U << (2U * i));
        }
    }
    return out;
}

uint8_t ssd1306_draw_text(uint8_t x, uint8_t page, const char *s, uint8_t scale)
{
    if ((scale < 1U) || (scale > 2U) || (page + scale > OLED_PAGES)) {
        return x;
    }
    for (; *s != '\0'; s++) {
        const uint8_t *g = font6x8_glyph(*s);
        for (uint8_t col = 0U; col < FONT6X8_WIDTH; col++) {
            const uint8_t bits = (col < FONT6X8_GLYPH_COLS) ? g[col] : 0U;
            for (uint8_t rep = 0U; rep < scale; rep++) {
                if (x >= OLED_WIDTH) {
                    return x;
                }
                if (scale == 1U) {
                    s_fb[page][x] = bits;
                } else {
                    s_fb[page][x] = double_bits(bits & 0x0FU);
                    s_fb[page + 1U][x] = double_bits(bits >> 4);
                }
                x++;
            }
        }
        s_dirty |= (uint8_t)(((1U << scale) - 1U) << page);
    }
    return x;
}

status_t ssd1306_flush(void)
{
    if (s_addr == 0U) {
        return STATUS_NACK;
    }
    for (uint8_t page = 0U; page < OLED_PAGES; page++) {
        if ((s_dirty & (1U << page)) == 0U) {
            continue;
        }
        const uint8_t pos[] = {
            CMD_PAGE_ADDR(page),
            CMD_COL_LOW(OLED_COL_OFFSET),
            CMD_COL_HIGH(OLED_COL_OFFSET),
        };
        status_t st = send_cmds(pos, (uint8_t)sizeof(pos));
        if (st != STATUS_OK) {
            return st;
        }
        s_tx[0] = CTRL_DATA_STREAM;
        memcpy(&s_tx[1], s_fb[page], OLED_WIDTH);
        st = i2c_bus_write(s_addr, s_tx, (uint16_t)sizeof(s_tx));
        if (st != STATUS_OK) {
            return st;
        }
        s_dirty &= (uint8_t)~(1U << page);
    }
    return STATUS_OK;
}

status_t ssd1306_sleep(void)
{
    static const uint8_t seq[] = { CMD_DISPLAY_OFF, CMD_CHARGE_PUMP, 0x10U };
    return send_cmds(seq, (uint8_t)sizeof(seq));
}

status_t ssd1306_wake(void)
{
    static const uint8_t seq[] = { CMD_CHARGE_PUMP, 0x14U, CMD_DISPLAY_ON };
    return send_cmds(seq, (uint8_t)sizeof(seq));
}
