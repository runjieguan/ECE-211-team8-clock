#include <Arduino.h>
#include <I2CKeyPad.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "hardware.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr uint8_t SDA_PIN = 4;
constexpr uint8_t SCL_PIN = 5;
constexpr uint8_t SQW_PIN = 6;
#elif defined(CONFIG_IDF_TARGET_ESP32)
// Select Adafruit ESP32 Feather for the original HUZZAH32.
constexpr uint8_t SDA_PIN = SDA;
constexpr uint8_t SCL_PIN = SCL;
constexpr uint8_t SQW_PIN = 14;
#else
#error "Select an ESP32-S3 or Adafruit ESP32 Feather board."
#endif

namespace {

constexpr uint8_t KEYPAD_ADDRESS = 0x20;
constexpr char KEYPAD_MAP[] = "D#0*C987B654A321";
constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_ROWS = 2;
constexpr int8_t MP3_RX_GPIO = 18;
constexpr int8_t MP3_TX_GPIO = 17;

I2CKeyPad keypad(KEYPAD_ADDRESS);
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, LCD_ROWS);
bool i2cReady = false;
bool keypadReady = false;
bool lcdReady = false;
volatile uint32_t rtcTicks = 0;
portMUX_TYPE tickMux = portMUX_INITIALIZER_UNLOCKED;

}  // namespace

static void ARDUINO_ISR_ATTR onRtcRise(void);

static void ARDUINO_ISR_ATTR onRtcRise(void) {
  portENTER_CRITICAL_ISR(&tickMux);
  rtcTicks = rtcTicks + 1U;
  portEXIT_CRITICAL_ISR(&tickMux);
}

bool hw_i2c_read(uint8_t address, uint8_t reg, uint8_t *data, uint8_t length) {
  if (!i2cReady || length == 0 || data == nullptr) {
    return false;
  }
  Wire.beginTransmission(address);
  if (Wire.write(reg) != 1) {
    Wire.endTransmission();
    return false;
  }
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, length) != length) {
    return false;
  }
  for (uint8_t i = 0; i < length; ++i) {
    data[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool hw_i2c_write(uint8_t address, uint8_t reg, const uint8_t *data, uint8_t length) {
  if (!i2cReady || length == 0 || data == nullptr) {
    return false;
  }
  Wire.beginTransmission(address);
  const bool buffered = Wire.write(reg) == 1 && Wire.write(data, length) == length;
  return Wire.endTransmission() == 0 && buffered;
}

uint64_t hw_micros(void) {
  return static_cast<uint64_t>(esp_timer_get_time());
}

uint32_t hw_millis(void) {
  return millis();
}

uint32_t hw_rtc_ticks(void) {
  portENTER_CRITICAL(&tickMux);
  const uint32_t ticks = rtcTicks;
  portEXIT_CRITICAL(&tickMux);
  return ticks;
}

char hw_key(void) {
  if (!keypadReady) {
    return HW_KEY_ERROR;
  }
  const uint8_t sample = keypad.getKey();
  if (sample == I2C_KEYPAD_NOKEY) {
    return HW_KEY_NONE;
  }
  return sample < sizeof(KEYPAD_MAP) - 1 ? KEYPAD_MAP[sample] : HW_KEY_ERROR;
}

bool hw_keypad_ready(void) {
  return keypadReady;
}

bool hw_lcd_ready(void) {
  return lcdReady;
}

void hw_lcd_row(uint8_t row, const char *text) {
  if (lcdReady && row < LCD_ROWS) {
    lcd.setCursor(0, row);
    lcd.print(text);
  }
}

int hw_serial_available(void) {
  return Serial.available();
}

int hw_serial_read(void) {
  return Serial.read();
}

void hw_serial_write(const char *text) {
  Serial.print(text);
}

bool hw_audio_write(const uint8_t *data, size_t length) {
  return Serial2.write(data, length) == length;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, MP3_RX_GPIO, MP3_TX_GPIO);
  i2cReady = Wire.begin(SDA_PIN, SCL_PIN, 100000);
  if (i2cReady) {
    Wire.setTimeOut(50);
    Wire.beginTransmission(LCD_ADDRESS);
    lcdReady = Wire.endTransmission() == 0;
    if (lcdReady) {
      lcd.init();
      lcd.backlight();
    }
    keypadReady = keypad.begin();
  }
  pinMode(SQW_PIN, INPUT_PULLUP);
  attachInterrupt(SQW_PIN, onRtcRise, RISING);
  app_init();
}

void loop() {
  app_loop();
  delay(1);
}
