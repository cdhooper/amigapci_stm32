#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bec_cmd.h"
#include "main.h"
#include "msg.h"
#include "amigartc.h"
#include "config.h"
#include "crc32.h"
#include "printf.h"
#include "timer.h"
#include "uart.h"
#include "utils.h"
#include "version.h"

#define SWAP16(x)   __builtin_bswap16(x)
#define SWAP32(x)   __builtin_bswap32(x)
#define SWAP64(x)   __builtin_bswap64(x)

static const uint8_t testpatt_reply[] = {
    0xaa, 0x55, 0xcc, 0x33,
    0xee, 0x11, 0xff, 0x00,
    0x01, 0x02, 0x04, 0x08,
    0x10, 0x20, 0x40, 0x80,
    0xfe, 0xfd, 0xfb, 0xf7,
    0xef, 0xdf, 0xbf, 0x7f,
};

static void
msg_reply(uint rstatus, uint rlen1, const void *data1,
                        uint rlen2, const void *data2)
{
    uint32_t crc;
    uint rlen = rlen1 + rlen2;

    if (rlen + BEC_MSG_HDR_LEN + BEC_MSG_CRC_LEN > sizeof (bec_msg_outbuf)) {
        printf("msg len %x too long to send: %u\n", rlen, sizeof (bec_msg_outbuf));
        return;
    }
    bec_msg_out = 0;
    bec_msg_outbuf[0] = 0xcd;  // bec_magic
    bec_msg_outbuf[1] = 0x68;  // bec_magic
    bec_msg_outbuf[2] = rstatus;
    bec_msg_outbuf[3] = (uint8_t) (rlen >> 8);
    bec_msg_outbuf[4] = (uint8_t) rlen;
    if (rlen1 > 0)
        memcpy(&bec_msg_outbuf[5], data1, rlen1);
    if (rlen2 > 0)
        memcpy(&bec_msg_outbuf[5] + rlen1, data2, rlen2);

    /* CRC includes cmd + length + data */
    crc = crc32(0, bec_msg_outbuf + 2, rlen + BEC_MSG_HDR_LEN - 2);
    crc = SWAP32(crc);
    memcpy(&bec_msg_outbuf[BEC_MSG_HDR_LEN + rlen], &crc, BEC_MSG_CRC_LEN);
    bec_msg_out_max = (rlen + BEC_MSG_HDR_LEN + BEC_MSG_CRC_LEN) * 2;

    bec_msg_out_timeout = timer_tick_plus_msec(1000);

    /* Kick off reply by pre-loading first response byte */
    amigartc_reply_pending();
}

int
msg_process_fast(void)
{
    uint cmd    = bec_msg_inbuf[2];
    uint msglen = (bec_msg_inbuf[3] << 8) | bec_msg_inbuf[4];
    uint pos;
    uint32_t crc_expect;
    uint32_t crc_calc;

    memcpy(&crc_expect, bec_msg_inbuf + BEC_MSG_HDR_LEN + msglen,
           sizeof (crc_expect));
    crc_calc = crc32(0, &bec_msg_inbuf[2], BEC_MSG_HDR_LEN - 2 + msglen);
    crc_calc = SWAP32(crc_calc);
    if (crc_expect != crc_calc) {
        msg_reply(BEC_STATUS_CRC, 0, NULL, 0, NULL);
        sprintf(bec_errormsg_delayed,
               "cmd=%02x l=%04x CRC %08lx != calc %08lx\n",
               cmd, msglen, crc_expect, crc_calc);
        return (1);
    }
    switch (cmd) {
        case BEC_CMD_NULL:
            /* No reply */
            break;
        case BEC_CMD_NOP:
            msg_reply(BEC_STATUS_OK, 0, NULL, 0, NULL);
            break;
        case BEC_CMD_CONS_OUTPUT: {
            /* Output from STM32 */
            uint8_t *buf;
            uint16_t len;
            uint     maxlen = bec_msg_inbuf[BEC_MSG_HDR_LEN];
            len = ami_get_output(&buf, maxlen);
            msg_reply(BEC_STATUS_OK, len, buf, 0, NULL);
            break;
        }
        case BEC_CMD_CONS_INPUT: {
            /* Keystroke input to STM32 */
            for (pos = 0; pos < msglen; pos++)
                ami_rb_put(bec_msg_inbuf[BEC_MSG_HDR_LEN + pos]);
            msg_reply(BEC_STATUS_OK, 0, NULL, 0, NULL);
            break;
        }
        default:
            return (0);  // Push message to processing in the slow path
    }

    /* The message has been processed */
    return (1);
#if 0
    printf("cmd=%02x len=%04x", cmd, msglen);
    for (pos = 0; pos < msglen; pos++)
        printf(" %02x", bec_msg_inbuf[4 + pos]);
    printf("\n");
#endif
}

void
msg_process_slow(void)
{
    /* The message was already CRC-checked in the fast path */
    uint cmd    = bec_msg_inbuf[2];
    uint msglen = (bec_msg_inbuf[3] << 8) | bec_msg_inbuf[4];
    uint pos;

    switch (cmd) {
        case BEC_CMD_ID: {
            bec_id_t reply;
            uint temp[3];
            memset(&reply, 0, sizeof (reply));
            sscanf(version_str + 8, "%u.%u%n", &temp[0], &temp[1], &pos);
            reply.bid_version[0] = SWAP16(temp[0]);
            reply.bid_version[1] = SWAP16(temp[1]);
            if (pos == 0)
                pos = 18;
            else
                pos += 8 + 7;
            sscanf(version_str + pos, "%04u-%02u-%02u",
                   &temp[0], &temp[1], &temp[2]);
            reply.bid_date[0] = temp[0] / 100;
            reply.bid_date[1] = temp[0] % 100;
            reply.bid_date[2] = temp[1];
            reply.bid_date[3] = temp[2];
            pos += 11;
            sscanf(version_str + pos, "%02u:%02u:%02u",
                   &temp[0], &temp[1], &temp[2]);
            reply.bid_time[0] = temp[0];
            reply.bid_time[1] = temp[1];
            reply.bid_time[2] = temp[2];
            reply.bid_time[3] = 0;
            strcpy(reply.bid_serial, (const char *)cpu_serial_str);
            reply.bid_rev      = SWAP16(0x0001);     // Protocol version 0.1
            reply.bid_features = SWAP16(0x0000);     // Features
            strcpy(reply.bid_name, config.name);
            msg_reply(BEC_STATUS_OK, sizeof (reply), &reply, 0, NULL);
            break;
        }
        case BEC_CMD_UPTIME: {
            uint64_t now = timer_tick_get();
            uint64_t usec = timer_tick_to_usec(now);
            usec = SWAP64(usec);  // Big endian format
            msg_reply(BEC_STATUS_OK, sizeof (usec), &usec, 0, NULL);
            break;
        }
        case BEC_CMD_TESTPATT:
            msg_reply(BEC_STATUS_OK, sizeof (testpatt_reply), &testpatt_reply, 0, NULL);
            break;
        case BEC_CMD_LOOPBACK:
            msg_reply(cmd, msglen, bec_msg_inbuf + BEC_MSG_HDR_LEN, 0, NULL);
            break;
        case BEC_CMD_GET_MAP: {
            bec_keymap_t *req = (void *) &bec_msg_inbuf[BEC_MSG_HDR_LEN];
            uint count = req->bkm_count;
            uint start = req->bkm_start;

            /* Send max 240 bytes at a time (plus reply struct) */
            if (count > 240 / sizeof (config.keymap[0]))
                count = 240 / sizeof (config.keymap[0]);

            switch (req->bkm_which) {
                case BKM_WHICH_KEYMAP:
                    /* Don't send past the end */
                    if (count > ARRAY_SIZE(config.keymap) - start)
                        count = ARRAY_SIZE(config.keymap) - start;

                    req->bkm_len   = sizeof (config.keymap[0]);
                    req->bkm_count = count;
                    msg_reply(BEC_STATUS_OK, sizeof (*req), req,
                              count * req->bkm_len, &config.keymap[start]);
                    break;
                default:
                    msg_reply(BEC_STATUS_BADARG, 0, NULL, 0, NULL);
                    break;
            }
            break;
        }
        case BEC_CMD_SET_MAP: {
            bec_keymap_t *req = (void *) &bec_msg_inbuf[BEC_MSG_HDR_LEN];
            uint count   = req->bkm_count;
            uint cur     = req->bkm_start;
            uint maxkeys = req->bkm_len;
            uint maxkeys_local = sizeof (config.keymap[0]);
            uint key;
            uint8_t *data;

            switch (req->bkm_which) {
                case BKM_WHICH_KEYMAP:
                    data = (void *) (req + 1);
                    for (pos = 0; pos < count; pos++) {
                        uint32_t val = 0;
                        for (key = 0; key < maxkeys; key++) {
                            if (key >= maxkeys_local)
                                break;
                            val |= (data[key] << (8 * key));
                        }
                        config.keymap[cur] = val;
                        data += maxkeys;
                        cur++;
                    }
                    msg_reply(BEC_STATUS_OK, 0, NULL, 0, NULL);
                    break;
                default:
                    msg_reply(BEC_STATUS_BADARG, 0, NULL, 0, NULL);
                    break;
            }
            break;
        }
        default:
            msg_reply(BEC_STATUS_UNKCMD, 0, NULL, 0, NULL);
            break;
    }
}

void
msg_init(void)
{
#if 0
    /* Map PA4 to EXTI4 */
    exti_select_source(EXTI4, GPIOA);
    exti_set_trigger(EXTI4, EXTI_TRIGGER_FALLING);
    exti_enable_request(EXTI4);
    exti_reset_request(EXTI4);
    nvic_set_priority(NVIC_EXTI4_IRQ, 0x10);
    nvic_enable_irq(NVIC_EXTI4_IRQ);
#endif
#if 0
    exti_select_source(EXTI5, GPIOA);
    exti_set_trigger(EXTI5, EXTI_TRIGGER_FALLING);
    exti_enable_request(EXTI5);
    exti_reset_request(EXTI5);
    nvic_set_priority(NVIC_EXTI9_5_IRQ, 0x10);
    nvic_enable_irq(NVIC_EXTI9_5_IRQ);
#endif
}
