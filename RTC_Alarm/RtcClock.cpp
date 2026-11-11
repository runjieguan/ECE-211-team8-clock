#include "RtcClock.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

namespace {
constexpr uint8_t kAddress = 0x68;
constexpr uint8_t kControl = 0x0E;
constexpr uint8_t kStatus = 0x0F;
constexpr uint8_t kOwnedControlBits = 0xDF;
TwoWire *rtcBus = nullptr;
volatile uint32_t tickCount = 0;
portMUX_TYPE tickMux = portMUX_INITIALIZER_UNLOCKED;

void ARDUINO_ISR_ATTR onRtcRise() {
  portENTER_CRITICAL_ISR(&tickMux);
  tickCount = tickCount + 1U;
  portEXIT_CRITICAL_ISR(&tickMux);
}

bool readBytes(TwoWire &bus, uint8_t reg, uint8_t *data, uint8_t length) {
  bus.beginTransmission(kAddress);
  bus.write(reg);
  if (bus.endTransmission(false) != 0 || bus.requestFrom(kAddress, length) != length) {
    return false;
  }
  for (uint8_t i = 0; i < length; ++i) {
    data[i] = static_cast<uint8_t>(bus.read());
  }
  return true;
}

bool writeBytes(TwoWire &bus, uint8_t reg, const uint8_t *data, uint8_t length) {
  bus.beginTransmission(kAddress);
  bus.write(reg);
  bus.write(data, length);
  return bus.endTransmission() == 0;
}

uint8_t toBcd(uint8_t value) {
  return static_cast<uint8_t>((value / 10) * 16 + value % 10);
}

bool fromBcd(uint8_t bcd, uint8_t &value) {
  if ((bcd & 0x0F) > 9 || (bcd >> 4) > 9) {
    return false;
  }
  value = static_cast<uint8_t>((bcd >> 4) * 10 + (bcd & 0x0F));
  return true;
}

bool leapYear(uint16_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  constexpr uint8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return static_cast<uint8_t>(lengths[month - 1] + (month == 2 && leapYear(year)));
}

}  // namespace

uint32_t rtcSecondsSince2000(const RtcDateTime &value) {
  uint32_t days = 0;
  for (uint16_t year = 2000; year < value.year; ++year) {
    days += leapYear(year) ? 366U : 365U;
  }
  for (uint8_t month = 1; month < value.month; ++month) {
    days += daysInMonth(value.year, month);
  }
  days += value.day - 1U;
  return ((days * 24U + value.hour) * 60U + value.minute) * 60U + value.second;
}

bool rtcDateTimeValid(const RtcDateTime &value) {
  if (value.year < 2000 || value.year > 2099 || value.month < 1 || value.month > 12 ||
      value.day < 1 || value.hour > 23 || value.minute > 59 || value.second > 59) {
    return false;
  }
  return value.day <= daysInMonth(value.year, value.month);
}

bool rtcBegin(TwoWire &bus, uint8_t sqwPin) {
  if (rtcBus != nullptr || !GPIO_IS_VALID_GPIO(sqwPin)) {
    return false;
  }
  uint8_t control = 0;
  if (!readBytes(bus, kControl, &control, 1)) {
    return false;
  }
  // EOSC=0, BBSQW=0, RS2/RS1=0, INTCN=0, A2IE/A1IE=0.
  // Preserve CONV. Calendar and OSF are not overwritten at startup.
  control = static_cast<uint8_t>(control & ~kOwnedControlBits);
  if (!writeBytes(bus, kControl, &control, 1) ||
      !readBytes(bus, kControl, &control, 1) || (control & kOwnedControlBits) != 0) {
    return false;
  }
  rtcBus = &bus;
  pinMode(sqwPin, INPUT_PULLUP);
  attachInterrupt(sqwPin, onRtcRise, RISING);
  return true;
}

bool rtcGetStatus(uint8_t &control, uint8_t &status) {
  uint8_t registers[2] = {};
  if (rtcBus == nullptr || !readBytes(*rtcBus, kControl, registers, 2)) {
    return false;
  }
  control = registers[0];
  status = registers[1];
  return true;
}

bool rtcGetDateTime(RtcDateTime &value) {
  uint8_t raw[7] = {};
  if (rtcBus == nullptr || !readBytes(*rtcBus, 0x00, raw, 7)) {
    return false;
  }
  // Support 2000-2099. Reject century/reserved bits and invalid weekday values.
  if ((raw[0] & 0x80) || (raw[1] & 0x80) || (raw[2] & 0x80) ||
      raw[3] < 1 || raw[3] > 7 || (raw[4] & 0xC0) || (raw[5] & 0xE0)) {
    return false;
  }
  RtcDateTime decoded = {};
  uint8_t year = 0;
  uint8_t hour = 0;
  if (!fromBcd(raw[0], decoded.second) || !fromBcd(raw[1], decoded.minute) ||
      !fromBcd(raw[4], decoded.day) || !fromBcd(raw[5], decoded.month) ||
      !fromBcd(raw[6], year)) {
    return false;
  }
  if (raw[2] & 0x40) {
    if (!fromBcd(raw[2] & 0x1F, hour) || hour < 1 || hour > 12) {
      return false;
    }
    decoded.hour = static_cast<uint8_t>(hour % 12 + ((raw[2] & 0x20) ? 12 : 0));
  } else {
    if (!fromBcd(raw[2] & 0x3F, hour)) {
      return false;
    }
    decoded.hour = hour;
  }
  decoded.year = static_cast<uint16_t>(2000U + year);
  if (!rtcDateTimeValid(decoded)) {
    return false;
  }
  value = decoded;
  return true;
}

bool rtcSetDateTime(const RtcDateTime &value) {
  if (rtcBus == nullptr || !rtcDateTimeValid(value)) {
    return false;
  }
  const uint32_t wanted = rtcSecondsSince2000(value);
  const uint32_t days = wanted / 86400U;
  // Sunday=1 ... Saturday=7; 2000-01-01 was Saturday.
  const uint8_t weekday = static_cast<uint8_t>((days + 6U) % 7U + 1U);
  const uint8_t raw[7] = {
      toBcd(value.second), toBcd(value.minute), toBcd(value.hour), weekday,
      toBcd(value.day), toBcd(value.month), toBcd(static_cast<uint8_t>(value.year - 2000))};
  if (!writeBytes(*rtcBus, 0x00, raw, 7)) {
    return false;
  }

  RtcDateTime readback = {};
  if (!rtcGetDateTime(readback)) {
    return false;
  }
  const uint32_t actual = rtcSecondsSince2000(readback);
  if (actual < wanted || actual - wanted > 2U) {
    return false;
  }

  uint8_t status = 0;
  if (!readBytes(*rtcBus, kStatus, &status, 1)) {
    return false;
  }
  // Clear OSF only after a verified calendar write. Ones preserve alarm flags.
  status = static_cast<uint8_t>((status & 0x7F) | 0x03);
  if (!writeBytes(*rtcBus, kStatus, &status, 1) ||
      !readBytes(*rtcBus, kStatus, &status, 1)) {
    return false;
  }
  return (status & 0x80) == 0;
}

uint32_t getRtcTicks() {
  portENTER_CRITICAL(&tickMux);
  const uint32_t result = tickCount;
  portEXIT_CRITICAL(&tickMux);
  return result;
}
