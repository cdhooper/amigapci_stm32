/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * Generic POSIX function emulation.
 */

#ifndef _UTILS_H
#define _UTILS_H

#define ADDR8(x)    ((uint8_t *)  ((uintptr_t)(x)))
#define ADDR16(x)   ((uint16_t *) ((uintptr_t)(x)))
#define ADDR32(x)   ((uint32_t *) ((uintptr_t)(x)))

#define BIT(x)      (1U << (x))

#define ARRAY_SIZE(x) (int)((sizeof (x) / sizeof ((x)[0])))

/*
 * Compile-time asserts
 */
#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
#define CC_ASSERT(e, n) enum { \
            ASSERT_CONCAT(ASSERT_CONCAT(ASSERT_CONCAT(assert_line_, __LINE__), \
            ASSERT_CONCAT(_, __COUNTER__)), n) = 1/(!!(e)) \
        }
#define CC_ASSERT_SIZE(type, size) CC_ASSERT(sizeof (type) == (size), type)
#define CC_ASSERT_ARRAY_SIZE(type, nelem) \
        CC_ASSERT(ARRAY_SIZE(type) == (nelem), type)

void reset_dfu(int in_rom);
void reset_cpu(void);
void reset_check(void);
void get_reset_reason(void);
void show_reset_reason(void);
void identify_cpu(void);

extern uint8_t cold_poweron;

#endif /* _UTILS_H */
