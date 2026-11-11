#include <Arduino.h>
#include <Wire.h>
#include "rtc_io.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr uint8_t SDA_PIN = 4;
constexpr uint8_t SCL_PIN = 5;
constexpr uint8_t SQW_PIN = 6;
#elif defined(CONFIG_IDF_TARGET_ESP32)
constexpr uint8_t SDA_PIN = SDA;
constexpr uint8_t SCL_PIN = SCL;
constexpr uint8_t SQW_PIN = 14;  // Adafruit ESP32 Feather wiring.
#else
#error "Select an ESP32-S3 or Adafruit ESP32 Feather board."
#endif

static bool wireReady;
static volatile uint32_t ticks;
static portMUX_TYPE tickMux = portMUX_INITIALIZER_UNLOCKED;
static void ARDUINO_ISR_ATTR countSqwRise(void);

static void ARDUINO_ISR_ATTR countSqwRise(void) {
  portENTER_CRITICAL_ISR(&tickMux);
  ticks = ticks + 1;
  portEXIT_CRITICAL_ISR(&tickMux);
}

uint32_t hw_sqw_ticks(void) {
  portENTER_CRITICAL(&tickMux);
  const uint32_t count = ticks;
  portEXIT_CRITICAL(&tickMux);
  return count;
}

bool hw_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, uint8_t length) {
  if (!wireReady) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, length) != length) return false;
  for (uint8_t i = 0; i < length; ++i) data[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

bool hw_i2c_write(uint8_t address, uint8_t reg, const uint8_t *data, uint8_t length) {
  if (!wireReady) return false;
  Wire.beginTransmission(address);
  const bool buffered = Wire.write(reg) == 1 && Wire.write(data, length) == length;
  return Wire.endTransmission() == 0 && buffered;
}

int hw_serial_read(void) {
  return Serial.read();
}

void hw_serial_write(const char *text) {
  Serial.print(text);
}

void setup() {
  Serial.begin(115200);
  wireReady = Wire.begin(SDA_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(50);
  pinMode(SQW_PIN, INPUT_PULLUP);
  attachInterrupt(SQW_PIN, countSqwRise, RISING);
  rtc_setup();
}

void loop() {
  rtc_loop();
  delay(1);
}
