#include "ClockAlarm.h"

namespace {
constexpr uint32_t kSecondsPerMinute = 60;
constexpr uint32_t kSecondsPerDay = 86400;
constexpr uint32_t kEndOf2099 = 3155760000U;  // Exclusive, at 2100-01-01 00:00:00.
}  // namespace

bool ClockAlarm::arm(uint32_t nowSeconds, uint8_t hour, uint8_t minute) {
  if (nowSeconds >= kEndOf2099 || hour >= 24 || minute >= 60) {
    return false;
  }

  const uint32_t dayStart = nowSeconds - nowSeconds % kSecondsPerDay;
  const uint32_t minuteOfDay = static_cast<uint32_t>(hour) * 60 + minute;
  uint32_t dueSeconds = dayStart + minuteOfDay * kSecondsPerMinute;
  if (dueSeconds <= nowSeconds) {
    dueSeconds += kSecondsPerDay;
  }
  if (dueSeconds >= kEndOf2099) {
    return false;
  }

  hour_ = hour;
  minute_ = minute;
  dueSeconds_ = dueSeconds;
  armed_ = true;
  return true;
}

void ClockAlarm::rebase(uint32_t nowSeconds) {
  if (armed_ && !arm(nowSeconds, hour_, minute_)) {
    cancel();
  }
}

void ClockAlarm::cancel() {
  armed_ = false;
}

bool ClockAlarm::isArmed() const {
  return armed_;
}

uint8_t ClockAlarm::hour() const {
  return hour_;
}

uint8_t ClockAlarm::minute() const {
  return minute_;
}

bool ClockAlarm::takeDue(uint32_t nowSeconds) {
  if (!armed_ || nowSeconds < dueSeconds_ || nowSeconds >= kEndOf2099) {
    return false;
  }
  cancel();
  return nowSeconds - dueSeconds_ < kSecondsPerMinute;
}
