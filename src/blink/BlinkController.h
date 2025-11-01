#pragma once

#include <Arduino.h>

namespace blink {

enum class LedMode : uint8_t {
  Normal,
  WifiConnecting,
  WifiError,
  DataError,
};

class BlinkController {
public:
  void begin(uint8_t pin, uint8_t activeLevel, uint8_t inactiveLevel);
  void setMode(LedMode mode);
  void update();

private:
  struct Segment {
    uint16_t duration_ms;
    bool led_on;
  };

  struct BlinkPattern {
    const Segment *segments;
    size_t length;
  };

  static const Segment NORMAL_SEGMENTS[];
  static const Segment WIFI_CONNECTING_SEGMENTS[];
  static const Segment WIFI_ERROR_SEGMENTS[];
  static const Segment DATA_ERROR_SEGMENTS[];

  static const BlinkPattern NORMAL_PATTERN;
  static const BlinkPattern WIFI_CONNECTING_PATTERN;
  static const BlinkPattern WIFI_ERROR_PATTERN;
  static const BlinkPattern DATA_ERROR_PATTERN;

  void applyCurrentSegment();
  const BlinkPattern &patternForMode(LedMode mode) const;

  uint8_t pin_{0};
  uint8_t activeLevel_{LOW};
  uint8_t inactiveLevel_{HIGH};
  LedMode currentMode_{LedMode::Normal};
  bool patternInitialized_{false};
  const BlinkPattern *pattern_{nullptr};
  size_t patternIndex_{0};
  unsigned long lastTransition_{0};
};

}  // namespace blink

