/**
 * @file    trace.h
 * @brief   RAM ring-buffer debug log (Debug builds only).
 *
 * There is no SWO pin, so view g_trace in the debugger (Live Expressions or
 * Memory view). In Release builds TRACE() compiles to nothing.
 * Call only from the main loop, never from an ISR.
 */
#ifndef APP_TRACE_H
#define APP_TRACE_H

#include <stdint.h>
#include "config.h"

typedef enum {
    TR_BOOT = 1,        /* arg = RCC_CSR reset flags */
    TR_RTC_FIRST_BOOT,  /* calendar initialised to the first-boot date */
    TR_RTC_WAKE,        /* arg = RTC wake count */
    TR_KEY_IRQ,         /* arg = key IRQ count */
    TR_ERROR,           /* arg = status_t */
    TR_I2C_ERROR,       /* arg = (status_t << 16) | HAL I2C error code */
    TR_PN532_ERROR,     /* arg = (step << 16) | status_t */
    TR_I2C_RECOVER,     /* arg = (SCL << 1) | SDA line levels before recovery */
} trace_code_t;

typedef struct {
    uint32_t tick;      /* HAL_GetTick() in ms */
    uint16_t code;      /* trace_code_t */
    uint16_t reserved;
    uint32_t arg;
} trace_entry_t;

typedef struct {
    uint32_t head;      /* index of the next slot to write */
    uint32_t total;     /* entries written since boot (head wraps, total doesn't) */
    trace_entry_t entries[TRACE_DEPTH];
} trace_buf_t;

#if APP_DEBUG
extern trace_buf_t g_trace;
void trace_init(void);
void trace_log(trace_code_t code, uint32_t arg);
#define TRACE(code, arg)  trace_log((code), (uint32_t)(arg))
#else
#define trace_init()      ((void)0)
#define TRACE(code, arg)  do { (void)(code); (void)(arg); } while (0)
#endif

#endif /* APP_TRACE_H */
