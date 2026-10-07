/**
 * @file    pn532.c
 * @brief   NXP PN532 NFC controller over I2C (UM0701-02 Rev 02).
 *
 * Link layer (UM0701-02 6.2.1, 6.2.4):
 *   host -> PN532: 00 00 FF LEN LCS D4 CMD data... DCS 00
 *   PN532 -> host: every read starts with a status byte, bit 0 = RDY.
 *   Poll the status byte (1-byte reads, each ended with STOP) until RDY,
 *   then read status + ACK frame (00 00 FF 00 FF 00), poll again, then read
 *   status + response frame (TFI D5, CMD + 1).
 * The PN532 may NACK its address while busy; that counts as "not ready".
 */
#include "pn532.h"
#include <string.h>
#include "config.h"
#include "i2c_bus.h"
#include "stm32f4xx_hal.h"
#include "trace.h"

#define TFI_HOST_TO_PN532   0xD4U
#define TFI_PN532_TO_HOST   0xD5U
#define TFI_ERROR_FRAME     0x7FU   /* application-level error (6.2.1.5) */
#define STATUS_RDY          0x01U

#define CMD_GET_FIRMWARE    0x02U
#define CMD_SAM_CONFIG      0x14U
#define CMD_RF_CONFIG       0x32U
#define CMD_IN_LIST_PASSIVE 0x4AU
#define CMD_POWER_DOWN      0x16U

/* PowerDown WakeUpEnable bits (UM0701-02 7.2.11): bit 7 = I2C. RF level
 * detector (bit 3) stays off: it costs extra current (CLAUDE.md 6.5). */
#define WAKEUP_I2C          0x80U
/* "The PN532 needs approximately 1 ms to get into Power Down mode, after the
 * command response" (7.2.11): wait before the next command. */
#define POWER_DOWN_SETTLE_MS 2U

/* Status byte (UM0701-02 7.1): bits 0-5 = error code, 0 = success. */
#define STATUS_ERROR_CODE_MASK 0x3FU

#define RF_ITEM_FIELD       0x01U
#define RF_ITEM_MAX_RETRIES 0x05U
#define SAM_MODE_NORMAL     0x01U
#define BRTY_106_TYPE_A     0x00U
#define MXRTY_ATR_DEFAULT   0xFFU   /* Table 18 defaults */
#define MXRTY_PSL_DEFAULT   0x01U

#define PARAMS_MAX          16U
#define RESP_DATA_MAX       48U     /* InListPassiveTarget with 10-byte UID + ATS */

/* Steps reported in TR_PN532_ERROR traces. */
enum { STEP_WRITE = 1, STEP_ACK, STEP_RESP, STEP_PARSE };

static const uint8_t s_ack_frame[6] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };

static status_t write_frame(uint8_t cmd, const uint8_t *params, uint8_t plen)
{
    uint8_t buf[8U + PARAMS_MAX];
    if (plen > PARAMS_MAX) {
        return STATUS_INVALID;
    }
    const uint8_t len = (uint8_t)(2U + plen);       /* TFI + CMD + params */
    uint8_t n = 0U;
    buf[n++] = 0x00U;                               /* preamble */
    buf[n++] = 0x00U;                               /* start code */
    buf[n++] = 0xFFU;
    buf[n++] = len;
    buf[n++] = (uint8_t)(0x100U - len);             /* LCS: LEN + LCS = 0 */
    buf[n++] = TFI_HOST_TO_PN532;
    buf[n++] = cmd;
    uint8_t sum = (uint8_t)(TFI_HOST_TO_PN532 + cmd);
    for (uint8_t i = 0U; i < plen; i++) {
        buf[n++] = params[i];
        sum = (uint8_t)(sum + params[i]);
    }
    buf[n++] = (uint8_t)(0x100U - sum);             /* DCS: TFI + data + DCS = 0 */
    buf[n++] = 0x00U;                               /* postamble */

    status_t st = i2c_bus_write(I2C_ADDR_PN532, buf, n);
    if (st == STATUS_NACK) {
        /* The first transaction after PowerDown wakes the chip and may be
         * NACKed (CLAUDE.md 6.5). Retry once. */
        HAL_Delay(PN532_POLL_MS);
        st = i2c_bus_write(I2C_ADDR_PN532, buf, n);
    }
    return st;
}

static status_t wait_ready(uint32_t timeout_ms)
{
    const uint32_t start = HAL_GetTick();
    for (;;) {
        uint8_t status = 0U;
        const status_t st = i2c_bus_read(I2C_ADDR_PN532, &status, 1U);
        if ((st == STATUS_OK) && ((status & STATUS_RDY) != 0U)) {
            return STATUS_OK;
        }
        if ((st != STATUS_OK) && (st != STATUS_NACK)) {
            return st;                              /* bus fault */
        }
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return STATUS_TIMEOUT;
        }
        HAL_Delay(PN532_POLL_MS);
    }
}

static status_t read_ack(void)
{
    uint8_t buf[1U + sizeof(s_ack_frame)];
    status_t st = wait_ready(PN532_ACK_TIMEOUT_MS);
    if (st == STATUS_OK) {
        st = i2c_bus_read(I2C_ADDR_PN532, buf, (uint16_t)sizeof(buf));
    }
    if ((st == STATUS_OK) && (memcmp(&buf[1], s_ack_frame, sizeof(s_ack_frame)) != 0)) {
        st = STATUS_INVALID;
    }
    return st;
}

/* Sends an ACK frame, which makes the PN532 abort the current command
 * (UM0701-02 6.2.2.1 d). */
static void abort_command(void)
{
    (void)i2c_bus_write(I2C_ADDR_PN532, s_ack_frame, (uint16_t)sizeof(s_ack_frame));
}

static status_t read_response(uint8_t cmd, uint8_t *data, uint8_t data_max,
                              uint8_t *data_len, uint32_t timeout_ms)
{
    /* status + preamble + 00 FF + LEN + LCS + TFI + CMD + data + DCS + postamble */
    uint8_t buf[1U + 5U + 2U + RESP_DATA_MAX + 2U];
    const uint16_t n = (uint16_t)(1U + 5U + 2U + data_max + 2U);
    if (data_max > RESP_DATA_MAX) {
        return STATUS_INVALID;
    }

    status_t st = wait_ready(timeout_ms);
    if (st == STATUS_OK) {
        st = i2c_bus_read(I2C_ADDR_PN532, buf, n);
    }
    if (st != STATUS_OK) {
        return st;
    }

    /* Find the 00 FF start code after the status byte (preamble may be
     * absent, 6.2.1.6). */
    uint16_t i = 1U;
    while ((i + 1U < n) && !((buf[i] == 0x00U) && (buf[i + 1U] == 0xFFU))) {
        i++;
    }
    if (i + 4U >= n) {
        return STATUS_INVALID;
    }
    const uint8_t len = buf[i + 2U];
    const uint8_t lcs = buf[i + 3U];
    const uint16_t body = (uint16_t)(i + 4U);       /* index of TFI */
    if ((uint8_t)(len + lcs) != 0U) {
        return STATUS_INVALID;
    }
    if ((len == 1U) && (buf[body] == TFI_ERROR_FRAME)) {
        return STATUS_ERROR;
    }
    if ((len < 2U) || ((uint32_t)body + len + 1U > n)) {
        return STATUS_INVALID;
    }

    uint8_t sum = 0U;
    for (uint16_t k = 0U; k <= len; k++) {          /* TFI..PDn + DCS */
        sum = (uint8_t)(sum + buf[body + k]);
    }
    if ((sum != 0U) || (buf[body] != TFI_PN532_TO_HOST) ||
        (buf[body + 1U] != (uint8_t)(cmd + 1U))) {
        return STATUS_INVALID;
    }

    *data_len = (uint8_t)(len - 2U);
    memcpy(data, &buf[body + 2U], *data_len);
    return STATUS_OK;
}

static status_t command(uint8_t cmd, const uint8_t *params, uint8_t plen,
                        uint8_t *resp, uint8_t resp_max, uint8_t *resp_len,
                        uint32_t timeout_ms)
{
    uint8_t step = STEP_WRITE;
    status_t st = STATUS_ERROR;
    /* Two tries for the command + ACK: a frame sent while the chip wakes
     * from PowerDown can be lost. */
    for (uint32_t attempt = 0U; attempt < 2U; attempt++) {
        step = STEP_WRITE;
        st = write_frame(cmd, params, plen);
        if (st == STATUS_OK) {
            step = STEP_ACK;
            st = read_ack();
        }
        if (st == STATUS_OK) {
            break;
        }
    }
    if (st == STATUS_OK) {
        step = STEP_RESP;
        st = read_response(cmd, resp, resp_max, resp_len, timeout_ms);
        if (st == STATUS_TIMEOUT) {
            abort_command();
        }
    }
    if (st != STATUS_OK) {
        TRACE(TR_PN532_ERROR, ((uint32_t)step << 16) | (uint32_t)st);
    }
    return st;
}

status_t pn532_power_down(void)
{
    const uint8_t p[] = { WAKEUP_I2C };
    uint8_t r[1];
    uint8_t rlen = 0U;
    status_t st = command(CMD_POWER_DOWN, p, (uint8_t)sizeof(p), r, (uint8_t)sizeof(r), &rlen,
                          PN532_RESP_TIMEOUT_MS);
    if ((st == STATUS_OK) && ((rlen != 1U) || ((r[0] & STATUS_ERROR_CODE_MASK) != 0U))) {
        st = STATUS_ERROR;
    }
    HAL_Delay(POWER_DOWN_SETTLE_MS);
    return st;
}

status_t pn532_init(pn532_fw_t *fw)
{
    pn532_fw_t tmp;
    pn532_fw_t *f = (fw != NULL) ? fw : &tmp;
    status_t st = pn532_get_firmware(f);
    if (st != STATUS_OK) {
        st = pn532_get_firmware(f);
    }
    if (st == STATUS_OK) {
        st = pn532_sam_config();
    }
    if (st == STATUS_OK) {
        st = pn532_set_passive_retries(PN532_PASSIVE_RETRIES);
    }
    if (st == STATUS_OK) {
        st = pn532_rf_field(false);
    }
    if (st == STATUS_OK) {
        st = pn532_power_down();
    }
    return st;
}

status_t pn532_get_firmware(pn532_fw_t *fw)
{
    uint8_t r[4];
    uint8_t rlen = 0U;
    status_t st = command(CMD_GET_FIRMWARE, NULL, 0U, r, (uint8_t)sizeof(r), &rlen,
                          PN532_RESP_TIMEOUT_MS);
    if ((st == STATUS_OK) && (rlen != 4U)) {
        st = STATUS_INVALID;
    }
    if (st == STATUS_OK) {
        fw->ic = r[0];
        fw->ver = r[1];
        fw->rev = r[2];
        fw->support = r[3];
    }
    return st;
}

status_t pn532_sam_config(void)
{
    /* Mode normal, Timeout 0 (only used in virtual card mode), IRQ 0:
     * P70_IRQ is not connected on this board. */
    const uint8_t p[] = { SAM_MODE_NORMAL, 0x00U, 0x00U };
    uint8_t r[1];
    uint8_t rlen = 0U;
    return command(CMD_SAM_CONFIG, p, (uint8_t)sizeof(p), r, (uint8_t)sizeof(r), &rlen,
                   PN532_RESP_TIMEOUT_MS);
}

status_t pn532_set_passive_retries(uint8_t retries)
{
    const uint8_t p[] = { RF_ITEM_MAX_RETRIES, MXRTY_ATR_DEFAULT, MXRTY_PSL_DEFAULT, retries };
    uint8_t r[1];
    uint8_t rlen = 0U;
    return command(CMD_RF_CONFIG, p, (uint8_t)sizeof(p), r, (uint8_t)sizeof(r), &rlen,
                   PN532_RESP_TIMEOUT_MS);
}

status_t pn532_rf_field(bool on)
{
    const uint8_t p[] = { RF_ITEM_FIELD, on ? 0x01U : 0x00U };
    uint8_t r[1];
    uint8_t rlen = 0U;
    return command(CMD_RF_CONFIG, p, (uint8_t)sizeof(p), r, (uint8_t)sizeof(r), &rlen,
                   PN532_RESP_TIMEOUT_MS);
}

status_t pn532_read_uid(uint8_t uid[PN532_UID_MAX], uint8_t *uid_len)
{
    const uint8_t p[] = { 0x01U, BRTY_106_TYPE_A };   /* MaxTg = 1 */
    uint8_t r[RESP_DATA_MAX];
    uint8_t rlen = 0U;
    *uid_len = 0U;

    status_t st = command(CMD_IN_LIST_PASSIVE, p, (uint8_t)sizeof(p), r, (uint8_t)sizeof(r),
                          &rlen, PN532_LIST_TIMEOUT_MS);
    if ((st != STATUS_OK) || (rlen < 1U) || (r[0] == 0U)) {
        return st;                                  /* error, or no target */
    }

    /* NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID1[] (7.3.5). */
    if (rlen < 6U) {
        return STATUS_INVALID;
    }
    const uint8_t idlen = r[5];
    if ((idlen == 0U) || (idlen > PN532_UID_MAX) || ((uint16_t)6U + idlen > rlen)) {
        return STATUS_INVALID;
    }
    memcpy(uid, &r[6], idlen);
    *uid_len = idlen;
    return STATUS_OK;
}
