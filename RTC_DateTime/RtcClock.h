#pragma once

#include <Arduino.h>
#include <Wire.h>

struct RtcDateTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

bool rtcBegin(TwoWire &bus, uint8_t sqwPin);
bool rtcGetDateTime(RtcDateTime &value);
bool rtcSetDateTime(const RtcDateTime &value);
bool rtcGetStatus(uint8_t &control, uint8_t &status);
bool rtcDateTimeValid(const RtcDateTime &value);
uint32_t getRtcTicks();
