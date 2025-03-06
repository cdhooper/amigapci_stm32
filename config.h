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

#define DF_KEYBOARD 0x00000001  // USB Keyboard debug
#define DF_MOUSE    0x00000002  // USB Mouse debug

typedef struct {
    uint32_t    magic;         // Structure magic
    uint32_t    crc;           // Structure CRC
    uint16_t    size;          // Structure size in bytes
    uint8_t     valid;         // Structure record is valid
    uint8_t     version;       // Structure version
    char        name[16];      // Unique name for this board
    uint8_t     keymap[256];   // USB key mappings to Amiga keys
    uint8_t     modkeymap[8];  // USB modifier key mappings to Amiga keys
    uint8_t     led_level;     // Power LED brightness (0 to 100)
    uint8_t     ps_on_mode;    // Power supply on at AC restored
    uint8_t     fan_speed;     // Fan speed percent (bit 7 = auto)
    uint8_t     fan_speed_min; // Fan (auto) speed at minimum temperature
    uint8_t     fan_temp_max;  // Temperature (C) for maximum fan speed
    uint8_t     fan_temp_min;  // Temperature (C) for minimum fan speed
    uint16_t    fan_rpm_max;   // Fan maximum RPM
    uint32_t    debug_flag;    // Debug flags (see DF_* above)
    int8_t      cpu_temp_bias; // CPU temperature bias
    uint8_t     unused[87];    // Unused
} config_t;

extern config_t config;

void config_updated(void);
void config_poll(void);
void config_read(void);

void config_name(const char *name);
void config_set_led(uint value);
void config_set_defaults(void);

#endif /* _CONFIG_H */
