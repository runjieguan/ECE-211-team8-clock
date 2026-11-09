#include "Stopwatch.h"
#include "esp_timer.h"

void Stopwatch::start() {
  if (running_) {
    return;
  }
  startedAtUs_ = esp_timer_get_time();
  running_ = true;
}

void Stopwatch::pause() {
  if (!running_) {
    return;
  }
  accumulatedUs_ += static_cast<uint64_t>(esp_timer_get_time() - startedAtUs_);
  running_ = false;
}

void Stopwatch::reset() {
  running_ = false;
  accumulatedUs_ = 0;
  startedAtUs_ = 0;
}

bool Stopwatch::isRunning() const {
  return running_;
}

uint64_t Stopwatch::elapsedUs() const {
  if (!running_) {
    return accumulatedUs_;
  }
  return accumulatedUs_ + static_cast<uint64_t>(esp_timer_get_time() - startedAtUs_);
}

uint64_t Stopwatch::elapsedCentiseconds() const {
  return elapsedUs() / 10000ULL;
}
