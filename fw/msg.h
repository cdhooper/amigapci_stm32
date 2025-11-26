/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * BEC message handling.
 */

#ifndef _MSG_H
#define _MSG_H

int  msg_process_fast(void);
void msg_process_slow(void);
void msg_process(void);
void msg_init(void);

#endif /* _MSG_H */
