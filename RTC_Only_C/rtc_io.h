#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool hw_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, uint8_t length);
bool hw_i2c_write(uint8_t address, uint8_t reg, const uint8_t *data, uint8_t length);
uint32_t hw_sqw_ticks(void);
int hw_serial_read(void);
void hw_serial_write(const char *text);
void rtc_setup(void);
void rtc_loop(void);

#ifdef __cplusplus
}
#endif
