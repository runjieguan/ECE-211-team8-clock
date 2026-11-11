#pragma once

#include <stdint.h>

class ClockAlarm {
 public:
  // Times are seconds since 2000-01-01, within 2000-2099.
  // A rejected setting leaves the current alarm intact.
  bool arm(uint32_t nowSeconds, uint8_t hour, uint8_t minute);
  void rebase(uint32_t nowSeconds);
  void cancel();

  bool isArmed() const;
  uint8_t hour() const;
  uint8_t minute() const;

  // Consume a due alarm once. An alarm missed by more than its minute expires.
  bool takeDue(uint32_t nowSeconds);

 private:
  bool armed_ = false;
  uint8_t hour_ = 0;
  uint8_t minute_ = 0;
  uint32_t dueSeconds_ = 0;
};
