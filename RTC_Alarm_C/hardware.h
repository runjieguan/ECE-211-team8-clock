#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HW_KEY_NONE '\0'
#define HW_KEY_ERROR '\x7f'

bool hw_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, uint8_t length);
bool hw_i2c_write(uint8_t address, uint8_t reg, const uint8_t *data, uint8_t length);
uint64_t hw_micros(void);
uint32_t hw_millis(void);
uint32_t hw_rtc_ticks(void);
char hw_key(void);
bool hw_keypad_ready(void);
bool hw_lcd_ready(void);
void hw_lcd_row(uint8_t row, const char *text);
int hw_serial_available(void);
int hw_serial_read(void);
void hw_serial_write(const char *text);
bool hw_audio_write(const uint8_t *data, size_t length);

void app_init(void);
void app_loop(void);

#ifdef __cplusplus
}
#endif
