/**
 * @file    fmt.c
 * @brief   Tiny string builder for display text (no printf).
 */
#include "fmt.h"

void fmt_init(fmt_t *f, char *buf, size_t cap)
{
    f->buf = buf;
    f->cap = cap;
    f->len = 0U;
    if (cap > 0U) {
        buf[0] = '\0';
    }
}

void fmt_char(fmt_t *f, char c)
{
    if (f->len + 1U < f->cap) {
        f->buf[f->len++] = c;
        f->buf[f->len] = '\0';
    }
}

void fmt_str(fmt_t *f, const char *s)
{
    while (*s != '\0') {
        fmt_char(f, *s++);
    }
}

void fmt_u32(fmt_t *f, uint32_t v)
{
    char tmp[10];
    uint8_t n = 0U;
    do {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0U);
    while (n > 0U) {
        fmt_char(f, tmp[--n]);
    }
}

void fmt_hex8(fmt_t *f, uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    fmt_char(f, hex[v >> 4]);
    fmt_char(f, hex[v & 0x0FU]);
}
