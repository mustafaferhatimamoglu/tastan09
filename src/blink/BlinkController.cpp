#include "blink/BlinkController.h"

namespace blink {
namespace {
constexpr uint16_t SHORT_PULSE_MS = 200;
constexpr uint16_t LONG_PULSE_MS = 600;
constexpr uint16_t PATTERN_PAUSE_MS = 1200;
}

const BlinkController::Segment BlinkController::NORMAL_SEGMENTS[] = {
    {1500, true},
    {1500, false},
};

const BlinkController::Segment BlinkController::WIFI_CONNECTING_SEGMENTS[] = {
    {SHORT_PULSE_MS, true},
    {SHORT_PULSE_MS, false},
    {SHORT_PULSE_MS, true},
    {LONG_PULSE_MS, false},
};

const BlinkController::Segment BlinkController::WIFI_ERROR_SEGMENTS[] = {
    {SHORT_PULSE_MS, true},
    {SHORT_PULSE_MS, false},
    {SHORT_PULSE_MS, true},
    {SHORT_PULSE_MS, false},
    {LONG_PULSE_MS, true},
    {LONG_PULSE_MS, false},
    {LONG_PULSE_MS, true},
    {LONG_PULSE_MS, false},
    {PATTERN_PAUSE_MS, false},
};

const BlinkController::Segment BlinkController::DATA_ERROR_SEGMENTS[] = {
    {SHORT_PULSE_MS, true},
    {SHORT_PULSE_MS, false},
    {LONG_PULSE_MS, true},
    {LONG_PULSE_MS, false},
    {LONG_PULSE_MS, true},
    {LONG_PULSE_MS, false},
    {3000, false},
};

const BlinkController::BlinkPattern BlinkController::NORMAL_PATTERN{
    BlinkController::NORMAL_SEGMENTS,
    sizeof(BlinkController::NORMAL_SEGMENTS) / sizeof(BlinkController::NORMAL_SEGMENTS[0])};
const BlinkController::BlinkPattern BlinkController::WIFI_CONNECTING_PATTERN{
    BlinkController::WIFI_CONNECTING_SEGMENTS,
    sizeof(BlinkController::WIFI_CONNECTING_SEGMENTS) /
        sizeof(BlinkController::WIFI_CONNECTING_SEGMENTS[0])};
const BlinkController::BlinkPattern BlinkController::WIFI_ERROR_PATTERN{
    BlinkController::WIFI_ERROR_SEGMENTS,
    sizeof(BlinkController::WIFI_ERROR_SEGMENTS) / sizeof(BlinkController::WIFI_ERROR_SEGMENTS[0])};
const BlinkController::BlinkPattern BlinkController::DATA_ERROR_PATTERN{
    BlinkController::DATA_ERROR_SEGMENTS,
    sizeof(BlinkController::DATA_ERROR_SEGMENTS) / sizeof(BlinkController::DATA_ERROR_SEGMENTS[0])};

void BlinkController::begin(uint8_t pin, uint8_t activeLevel, uint8_t inactiveLevel) {
  pin_ = pin;
  activeLevel_ = activeLevel;
  inactiveLevel_ = inactiveLevel;
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, inactiveLevel_);
}

void BlinkController::setMode(LedMode mode) {
  if (patternInitialized_ && mode == currentMode_) {
    return;
  }
  currentMode_ = mode;
  pattern_ = &patternForMode(mode);
  patternIndex_ = 0;
  applyCurrentSegment();
  lastTransition_ = millis();
  patternInitialized_ = true;
}

void BlinkController::update() {
  if (!pattern_ || pattern_->length == 0) {
    return;
  }

  const Segment &activeSegment = pattern_->segments[patternIndex_];
  const unsigned long now = millis();
  if (now - lastTransition_ < activeSegment.duration_ms) {
    return;
  }

  patternIndex_ = (patternIndex_ + 1) % pattern_->length;
  applyCurrentSegment();
  lastTransition_ = now;
}

void BlinkController::applyCurrentSegment() {
  if (!pattern_ || pattern_->length == 0) {
    return;
  }
  const Segment &segment = pattern_->segments[patternIndex_];
  digitalWrite(pin_, segment.led_on ? activeLevel_ : inactiveLevel_);
}

const BlinkController::BlinkPattern &BlinkController::patternForMode(LedMode mode) const {
  switch (mode) {
    case LedMode::WifiConnecting:
      return WIFI_CONNECTING_PATTERN;
    case LedMode::WifiError:
      return WIFI_ERROR_PATTERN;
    case LedMode::DataError:
      return DATA_ERROR_PATTERN;
    case LedMode::Normal:
    default:
      return NORMAL_PATTERN;
  }
}

}  // namespace blink

