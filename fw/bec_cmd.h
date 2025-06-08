/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * AmigaPCI command interface
 */

#ifndef _BEC_CMD_H
#define _BEC_CMD_H

/*
 * The AmigaPCI message interface is implemented on top of the Ricoh RP5C01
 * emulation. It provides a way for AmigaOS programs to interact with the
 * board management STM32 processor.
 *
 * An unused RP5C01 register (mode 9 register 9) is used as a FIFO for
 * message passing.  All messages and replies always begin with a magic
 * sequence and end with a 4-byte CRC.
 *
 * Message sequence (all data is in nibbles, high nibble first)
 *     Magic   0xc 0xd 0x6 0x8
 *     Command X X               Upper 2 bits are command options
 *     Length  X X               Payload length doesn't include header or CRC
 *     Payload [ X X * ]         Even number of nibbles in optional payload
 *     CRC     X X X X X X X X   32-bit in big endian format (includes Cmd+Len)
 */

/* Command codes sent to AmigaPCI STM32 */
#define BEC_CMD_NULL         0x00  // Do nothing (no reply)
#define BEC_CMD_NOP          0x01  // Do nothing but reply (success)
#define BEC_CMD_ID           0x01  // Send STM32 firmware ID and configuration
#define BEC_CMD_UPTIME       0x03  // Send STM32 uptime in microseconds (64-bit)
#define BEC_CMD_TESTPATT     0x05  // Send test pattern
#define BEC_CMD_LOOPBACK     0x06  // Reply with (exact) sent message
#define BEC_CMD_CONS_OUTPUT  0x07  // Receive STM32 console output
#define BEC_CMD_CONS_INPUT   0x08  // Send STM32 console input keystrokes
#define BEC_CMD_SET          0x09  // Set config value
#define BEC_CMD_GET          0x0a  // Get config value
#define BEC_CMD_SET_MAP      0x0b  // Set map (keyboard, mouse, etc, macros)
#define BEC_CMD_GET_MAP      0x0c  // Get map (keyboard, mouse, etc, macros)

/* Status codes returned by AmigaPCI STM32 */
#define BEC_STATUS_OK        0x00  // Success
#define BEC_STATUS_FAIL      0x01  // Generic failure
#define BEC_STATUS_CRC       0x02  // CRC failure
#define BEC_STATUS_UNKCMD    0x03  // Unknown command
#define BEC_STATUS_BADARG    0x04  // Bad command argument
#define BEC_STATUS_BADLEN    0x05  // Bad message length
#define BEC_STATUS_NODATA    0x06  // No data available
#define BEC_STATUS_LOCKED    0x07  // Resource locked
#define BEC_STATUS_TIMEOUT   0x08  // Timeout talking with BEC
#define BEC_STATUS_BADMAGIC  0x09  // Bad magic in response message
#define BEC_STATUS_REPLYLEN  0x0a  // Response message is too long
#define BEC_STATUS_REPLYCRC  0x0a  // Response message has bad CRC

#define BEC_MSG_HDR_LEN 8
#define BEC_MSG_CRC_LEN 8

typedef struct {
    uint16_t bid_version[2];       // BEC firmware version    (major-minor)
    uint8_t  bid_date[4];          // BEC firmware build date (cc-yy-mm-dd)
    uint8_t  bid_time[4];          // BEC firmware build time (hh-mm-ss-00)
    char     bid_serial[24];       // BEC serial number
    uint16_t bid_features;         // Available features
    uint16_t bid_rev;              // Protocol revision (00.01)
    char     bid_name[16];         // Unique name for this board
    uint8_t  bid_unused[24];       // Unused space
} bec_id_t;

typedef struct {
    uint8_t  bkm_which;            // Which keymap (see BKM_WHICH_*)
    uint8_t  bkm_start;            // First entry number
    uint8_t  bkm_len;              // Size of single entry
    uint8_t  bkm_count;            // Count of entries (0 = no entries)
} bec_keymap_t;

#define BKM_WHICH_KEYMAP       0x01
#define BKM_WHICH_MODKEYMAP    0x02
#define BKM_WHICH_BUTTONMAP    0x03
#define BKM_WHICH_SCROLLMAP    0x04
#define BKM_WHICH_JBUTTONMAP   0x05
#define BKM_WHICH_JDIRECTMAP   0x06

#endif  /* _BEC_CMD_H */

