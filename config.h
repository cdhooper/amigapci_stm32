/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * Config area management (non-volatile storage)
 */

#ifndef _CONFIG_H
#define _CONFIG_H

typedef struct {
    uint32_t    magic;       // Structure magic
    uint32_t    crc;         // Structure CRC
    uint16_t    size;        // Structure size in bytes
    uint8_t     valid;       // Structure record is valid
    uint8_t     version;     // Structure version
    char        name[16];    // Unique name for this board
    uint8_t     led_level;   // Power LED brightness (0 to 100)
    uint8_t     ps_on_mode;  // Power supply on at AC restored
    uint8_t     unused[99];  // Unused
} config_t;

extern config_t config;

void config_updated(void);
void config_poll(void);
void config_read(void);

void config_name(const char *name);
void config_set_led(uint value);

#endif /* _CONFIG_H */
