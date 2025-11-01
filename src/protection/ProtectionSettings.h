#pragma once

#include <Arduino.h>

namespace protection {

struct ProtectionSettings {
  float minC;
  float maxC;
  float hysteresisC;
  size_t minSamples;
  unsigned long renotifyIntervalMs;
};

bool validateProtectionSettings(const ProtectionSettings &settings);

}  // namespace protection

