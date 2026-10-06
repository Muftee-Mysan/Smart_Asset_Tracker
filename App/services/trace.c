/**
 * @file    trace.c
 * @brief   RAM ring-buffer debug log (Debug builds only).
 */
#include "trace.h"

#if APP_DEBUG

#include <string.h>
#include "stm32f4xx_hal.h"

trace_buf_t g_trace;

void trace_init(void)
{
    memset(&g_trace, 0, sizeof(g_trace));
}

void trace_log(trace_code_t code, uint32_t arg)
{
    trace_entry_t *e = &g_trace.entries[g_trace.head];
    e->tick = HAL_GetTick();
    e->code = (uint16_t)code;
    e->reserved = 0U;
    e->arg = arg;
    g_trace.head = (g_trace.head + 1U) % TRACE_DEPTH;
    g_trace.total++;
}

#endif /* APP_DEBUG */
