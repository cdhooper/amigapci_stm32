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
void amigartc_reset(void);
void amigartc_init(void);

#endif /* _AMIGARTC_H */
