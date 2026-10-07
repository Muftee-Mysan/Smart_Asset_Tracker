/**
 * @file    fmt.h
 * @brief   Tiny string builder for display text (no printf).
 */
#ifndef APP_FMT_H
#define APP_FMT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *buf;
    size_t cap;     /* including the terminating NUL */
    size_t len;
} fmt_t;

/* Starts an empty string in buf[cap]. Output is always NUL-terminated and
 * silently truncated. */
void fmt_init(fmt_t *f, char *buf, size_t cap);
void fmt_str(fmt_t *f, const char *s);
void fmt_char(fmt_t *f, char c);
void fmt_u32(fmt_t *f, uint32_t v);
void fmt_hex8(fmt_t *f, uint8_t v);

#endif /* APP_FMT_H */
