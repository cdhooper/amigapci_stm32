/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2025.
 *
 * ---------------------------------------------------------------------
 *
 * I2C functions.
 */

#ifndef _I2C_H
#define _I2C_H

/* i2c_read() and i2c_write() offset arguments */
#define I2C_FLAG_NONE           0x8000      // Do not send a device offset
#define I2C_FLAG_16BIT          0x4000      // 16-bit address
#define I2C_FLAG_BLOCK          0x2000      // Block mode transfer
#define I2C_FLAG_PEC            0x1000      // Use PEC (Packet Error Check)
#define I2C_FLAG_NO_RETRY       0x0800      // Do not retry on failure
#define I2C_FLAG_NO_CHECK       0x0400      // Do not read-verify accesses
#define I2C_FLAG_32BIT          0x0200      // 32-bit address

#define I2C_MAX_ADDR            0x80        // Number of I2C device addresses
#define I2C_MAX_BUS             2           // Number of I2C buses supported
#define I2C_ANY_BUS             0xff        // Match any I2C bus
#define I2C_ANY_DEV             0xff        // Match any I2C device

#define I2C_ADDR_INA219         0x40        // TI INA219 current sensor
#define I2C_ADDR_INA231         0x41        // TI INA231 current sensor
#define I2C_ADDR_TMP112         0x48        // TI TMP112 temperature sensor
#define I2C_ADDR_PS_PROM        0x50        // Power supply EEPROM
#define I2C_ADDR_PS             0x58        // Power supply PMBus interface

/*
 * I2C_BUS_* macros serve to make table and parameter constants more
 *           descriptive.  Implementation as to what is on each I2C bus
 *           depends upon the specific board.
 */
#define I2C_BUS_0               0
#define I2C_BUS_1               1

/* i2c_bus_avail() mode arguments */
typedef enum {
    RECOVER_NONE,   // Do not attempt bus recovery
    RECOVER_AUTO,   // Perform automatic recovery (if a bus is stuck)
    RECOVER_FORCE,  // Force recovery now (verbose)
} i2c_recover_t;

/**
 * i2c_read() reads bytes from an I2C device.
 *
 * @param [in]  bus    - I2C bus number.
 * @param [in]  dev    - I2C device address.
 *                       Note: an I2C device with a binary address of
 *                             1010000[0|1] would be specified as 0x50.
 * @param [in]  offset - Offset onto the device or I2C_FLAG_NONE if
 *                       an offset is not required.  I2C_FLAG_16BIT
 *                       may also be or'd with the value to specify the
 *                       device requires a 16-bit (two-byte) offset.
 * @param [in]  len    - Number of bytes to read.
 * @param [out] datap  - A pointer to a buffer containing space into
 *                       which len data bytes will be written.
 *
 * @return      RC_SUCCESS - read completed successfully.
 * @return      RC_TIMEOUT - A timeout occurred.
 * @return      RC_FAILURE - read failure.
 *
 * @see         i2c_write()
 */
rc_t i2c_read(uint bus, uint dev, uint offset, uint len, void *datap);

/**
 * i2c_write() write bytes to an I2C device.
 *
 * @param [in]  bus    - I2C bus number.
 * @param [in]  dev    - I2C device address.
 *                       Note: an I2C device with a binary address of
 *                             1010000[0|1] would be specified as 0x50.
 * @param [in]  offset - Offset onto the device or I2C_FLAG_NONE if
 *                       an offset is not required.  I2C_FLAG_16BIT
 *                       may also be or'd with the value to specify the
 *                       device requires a 16-bit (two-byte) offset.
 * @param [in]  len    - Number of bytes to write.
 * @param [in]  datap  - A pointer to a buffer to be written to the
 *                       specified I2C device.
 *
 * @return      RC_SUCCESS - write completed successfully.
 * @return      RC_TIMEOUT - A timeout occurred.
 * @return      RC_FAILURE - write failure.
 *
 * @see         i2c_read()
 */
rc_t i2c_write(uint bus, uint dev, uint offset, uint len, const void *datap);

/**
 * i2c_read_check() reads from an I2C device and reports an error message
 *                  if that read fails.
 *
 * @param [in]  bus    - I2C bus number.
 * @param [in]  dev    - I2C device address.
 * @param [in]  offset - The device string register to read.
 * @param [in]  len    - The maximum data buffer length.
 * @param [out] buf    - The buffer to return the read data.
 *
 * @return      RC_SUCCESS   - Read completed successfully.
 * @return      RC_TIMEOUT   - Device failed to respond.
 */
rc_t i2c_read_check(uint bus, uint dev, uint offset, uint len, void *buf);

/**
 * i2c_write_check() writes to an I2C device and reports an error message
 *                   if that write fails.
 *
 * @param [in]  bus    - I2C bus number.
 * @param [in]  dev    - I2C device address.
 * @param [in]  offset - The device string register to write.
 * @param [in]  len    - The maximum data buffer length.
 * @param [out] buf    - The buffer containing the data to write.
 *
 * @return      RC_SUCCESS   - Write completed successfully.
 * @return      RC_TIMEOUT   - Device failed to respond.
 */
rc_t i2c_write_check(uint bus, uint dev, uint offset, uint len,
                     const void *buf);

/**
 * i2c_init() configures and enables the I2C interfaces of the STM32 CPU.
 *
 * This function requires no arguments.
 *
 * @return      None.
 */
void i2c_init(void);

/**
 * cmd_i2c() provides the user "i2c" command.
 *
 * @param [in]  argc - Count of user arguments.
 * @param [in]  argv - Array of user arguments.
 *
 * @return      RC_SUCCESS   - Successful completion.
 * @return      RC_BAD_PARAM - Invalid parameter.
 * @return      RC_TIMEOUT   - I2C operation timeout.
 * @return      RC_FAILURE   - I2C failure.
 */
rc_t cmd_i2c(int argc, char * const argv[]);

/* "i2c" command help text */
extern const char cmd_i2c_help[];

/* Number of I2C buses on this board */
extern uint8_t i2c_bus_count;

#define HAVE_SPACE_I2C

#endif /* _I2C_H */
