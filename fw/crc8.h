/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * CRC-8 calculator.
 */

#ifndef _CRC8_H
#define _CRC8_H

uint8_t crc8(uint8_t oldcrc, const void *data, size_t len);

#endif /* _CRC8_H */
