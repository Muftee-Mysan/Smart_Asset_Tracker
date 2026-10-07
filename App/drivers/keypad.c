/**
 * @file    keypad.c
 * @brief   4x4 matrix keypad (Parallax 27899): scan, debounce, events.
 *
 * Parallax 27899 datasheet ("How it Works", Figure 1): pins 1-4 are columns,
 * pins 5-8 are rows; pin 1 = column 4 (A B C D), pin 8 = row 1 (1 2 3 A).
 * CLAUDE.md section 3.2 maps J9 pins to MCU pins assuming pin 1 -> pin 1.
 *
 * Columns are driven open-drain: the selected column is pulled low, the
 * others are released (high via the row pull-ups). With push-pull outputs,
 * two keys pressed in the same row would short a high column to a low one.
 */
#include "keypad.h"
#include "config.h"
#include "main.h"

#define ROWS 4U
#define COLS 4U

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} pin_t;

static const pin_t s_cols[COLS] = {
    { KP_C1_GPIO_Port, KP_C1_Pin },     /* col 1: 1 4 7 * */
    { KP_C2_GPIO_Port, KP_C2_Pin },     /* col 2: 2 5 8 0 */
    { KP_C3_GPIO_Port, KP_C3_Pin },     /* col 3: 3 6 9 # */
    { KP_C4_GPIO_Port, KP_C4_Pin },     /* col 4: A B C D */
};

static const pin_t s_rows[ROWS] = {
    { KP_R1_GPIO_Port, KP_R1_Pin },     /* row 1: 1 2 3 A */
    { KP_R2_GPIO_Port, KP_R2_Pin },     /* row 2: 4 5 6 B */
    { KP_R3_GPIO_Port, KP_R3_Pin },     /* row 3: 7 8 9 C */
    { KP_R4_GPIO_Port, KP_R4_Pin },     /* row 4: * 0 # D */
};

/* Key labels [row][col]. If keys come out mirrored on the hardware, fix this
 * table (CLAUDE.md 3.2), not the wiring. */
static const char s_map[ROWS][COLS] = {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' },
};

/* EXTI lines of the row pins (PB10, PB12, PB13, PB5). */
#define ROW_EXTI_MASK   (KP_R1_Pin | KP_R2_Pin | KP_R3_Pin | KP_R4_Pin)

/* Consecutive equal samples needed: N samples one tick apart span
 * (N - 1) ticks, so 20 ms at 10 ms/tick needs 3 samples. */
#define DEBOUNCE_TICKS  ((KEY_DEBOUNCE_MS + TICK_PERIOD_MS - 1U) / TICK_PERIOD_MS + 1U)

static char s_stable = KEY_NONE;    /* debounced state */
static char s_candidate = KEY_NONE; /* raw state being confirmed */
static uint8_t s_count;             /* ticks s_candidate has been seen */

static void delay_us(uint32_t us)
{
    /* DWT cycle counter; SystemCoreClock = 16 MHz. */
    const uint32_t start = DWT->CYCCNT;
    const uint32_t cycles = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < cycles) {
    }
}

void keypad_awake_config(void)
{
    /* Enable the DWT cycle counter for delay_us(). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Rows: stop EXTI interrupts while scanning; clear anything pending. */
    EXTI->IMR &= ~(uint32_t)ROW_EXTI_MASK;
    EXTI->PR = ROW_EXTI_MASK;

    /* Columns: open-drain outputs, all released. */
    GPIO_InitTypeDef gi = {0};
    gi.Mode = GPIO_MODE_OUTPUT_OD;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    for (uint8_t c = 0U; c < COLS; c++) {
        HAL_GPIO_WritePin(s_cols[c].port, s_cols[c].pin, GPIO_PIN_SET);
        gi.Pin = s_cols[c].pin;
        HAL_GPIO_Init(s_cols[c].port, &gi);
    }

    s_stable = KEY_NONE;
    s_candidate = KEY_NONE;
    s_count = 0U;
}

char keypad_scan_raw(void)
{
    char found = KEY_NONE;
    uint8_t hits = 0U;

    for (uint8_t c = 0U; c < COLS; c++) {
        HAL_GPIO_WritePin(s_cols[c].port, s_cols[c].pin, GPIO_PIN_RESET);
        delay_us(KEY_SETTLE_US);
        for (uint8_t r = 0U; r < ROWS; r++) {
            if (HAL_GPIO_ReadPin(s_rows[r].port, s_rows[r].pin) == GPIO_PIN_RESET) {
                found = s_map[r][c];
                hits++;
            }
        }
        HAL_GPIO_WritePin(s_cols[c].port, s_cols[c].pin, GPIO_PIN_SET);
    }

    return (hits == 1U) ? found : KEY_NONE;
}

bool keypad_tick(key_event_t *ev)
{
    const char raw = keypad_scan_raw();

    if (raw != s_candidate) {
        s_candidate = raw;
        s_count = 1U;
        return false;
    }
    if (s_count < DEBOUNCE_TICKS) {
        s_count++;
    }
    if ((s_count < DEBOUNCE_TICKS) || (s_candidate == s_stable)) {
        return false;
    }

    /* Debounced change. A direct key-to-key change reports UP first; the
     * DOWN for the new key follows on the next tick. */
    if (s_stable != KEY_NONE) {
        ev->type = KEY_EV_UP;
        ev->key = s_stable;
        s_stable = KEY_NONE;
        s_count = 0U;
        s_candidate = KEY_NONE;
        return true;
    }
    ev->type = KEY_EV_DOWN;
    ev->key = s_candidate;
    s_stable = s_candidate;
    return true;
}
