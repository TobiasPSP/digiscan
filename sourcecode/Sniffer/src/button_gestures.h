#pragma once
#include <stdint.h>

namespace sniffer {
enum class Gesture : uint8_t { None, Short, Double, Long };
class ButtonGestures {
 public:
  static constexpr uint32_t DEBOUNCE_MS = 30, DOUBLE_MS = 350, LONG_MS = 1200;
  Gesture update(bool rawPressed, uint32_t now_ms) {
    if (rawPressed != candidate_) { candidate_ = rawPressed; candidate_ms_ = now_ms; }
    if (candidate_ != pressed_ && now_ms - candidate_ms_ >= DEBOUNCE_MS) {
      pressed_ = candidate_;
      if (pressed_) {
        pressed_ms_ = now_ms;
        longHandled_ = false;
        second_ = waiting_ && now_ms - released_ms_ <= DOUBLE_MS;
        if (second_) waiting_ = false;
      } else if (!longHandled_) {
        if (second_) { second_ = waiting_ = false; return Gesture::Double; }
        released_ms_ = now_ms;
        waiting_ = true;
      }
    }
    if (pressed_ && !longHandled_ && now_ms - pressed_ms_ >= LONG_MS) {
      longHandled_ = true;
      waiting_ = second_ = false;
      return Gesture::Long;
    }
    if (waiting_ && now_ms - released_ms_ > DOUBLE_MS) {
      waiting_ = false;
      return Gesture::Short;
    }
    return Gesture::None;
  }
 private:
  bool candidate_ = false, pressed_ = false, waiting_ = false;
  bool second_ = false, longHandled_ = false;
  uint32_t candidate_ms_ = 0, pressed_ms_ = 0, released_ms_ = 0;
};
} // namespace sniffer
