#pragma once

#include <Arduino.h>

enum schedular_unit_t {
  _Millis,
  _Seconds
};

class Schedular {
 public:
  explicit Schedular(schedular_unit_t unit) : unit_(unit) {}

  void start(long offset = 0) {
    last_ = nowMs() + scale(offset);
    started_ = true;
  }

  bool check(long period) {
    if (!started_) {
      start();
    }

    if (period < 0) {
      last_ = nowMs() + scale(period);
      return false;
    }

    const unsigned long now = nowMs();
    const unsigned long duration = static_cast<unsigned long>(scale(period));
    if (now - last_ >= duration) {
      last_ = now;
      return true;
    }
    return false;
  }

 private:
  unsigned long nowMs() const { return millis(); }

  long scale(long value) const {
    return unit_ == _Seconds ? value * 1000L : value;
  }

  schedular_unit_t unit_;
  unsigned long last_ = 0;
  bool started_ = false;
};

