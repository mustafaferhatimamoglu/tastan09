#pragma once

#include <Arduino.h>

namespace sensor {

struct MeasurementStats {
  float min = 0.0f;
  float max = 0.0f;
  float average = 0.0f;
  float last = 0.0f;
  size_t count = 0;
};

class MeasurementAggregator {
public:
  void reset();
  void addSample(float value);
  bool hasSamples() const;
  MeasurementStats stats() const;

private:
  float min_{0.0f};
  float max_{0.0f};
  float sum_{0.0f};
  float last_{0.0f};
  size_t count_{0};
};

}  // namespace sensor

