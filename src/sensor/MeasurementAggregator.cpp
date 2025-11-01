#include "sensor/MeasurementAggregator.h"

namespace sensor {

void MeasurementAggregator::reset() {
  min_ = 0.0f;
  max_ = 0.0f;
  sum_ = 0.0f;
  last_ = 0.0f;
  count_ = 0;
}

void MeasurementAggregator::addSample(float value) {
  if (count_ == 0) {
    min_ = max_ = value;
  } else {
    if (value < min_) {
      min_ = value;
    }
    if (value > max_) {
      max_ = value;
    }
  }
  sum_ += value;
  last_ = value;
  ++count_;
}

bool MeasurementAggregator::hasSamples() const {
  return count_ > 0;
}

MeasurementStats MeasurementAggregator::stats() const {
  MeasurementStats s;
  s.count = count_;
  if (count_ > 0) {
    s.min = min_;
    s.max = max_;
    s.last = last_;
    s.average = sum_ / static_cast<float>(count_);
  }
  return s;
}

}  // namespace sensor

