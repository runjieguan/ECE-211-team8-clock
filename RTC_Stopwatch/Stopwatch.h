#pragma once

#include <stdint.h>

// Use from one main loop/task. The object's state survives screen changes.
class Stopwatch {
 public:
  void start();
  void pause();
  void reset();
  bool isRunning() const;
  uint64_t elapsedUs() const;
  uint64_t elapsedCentiseconds() const;

 private:
  bool running_ = false;
  uint64_t accumulatedUs_ = 0;
  int64_t startedAtUs_ = 0;
};
