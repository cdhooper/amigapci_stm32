/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Amiga Real-Time Clock emulation.
 */

#ifndef _AMIGARTC_H
#define _AMIGARTC_H

void amigartc_snoop(int debug);
void amigartc_print(void);
void amigartc_log(void);
void amigartc_poll(void);
void amigartc_reply_pending(void);
void amigartc_reset(void);
void amigartc_init(void);

extern uint8_t  bec_msg_inbuf[280];
extern uint8_t  bec_msg_outbuf[280];
extern uint     bec_msg_out;        // Current send position in nibbles
extern uint     bec_msg_out_max;    // Message length in nibbles
extern uint64_t bec_msg_out_timeout;
extern char     bec_errormsg_delayed[80];

#endif /* _AMIGARTC_H */
