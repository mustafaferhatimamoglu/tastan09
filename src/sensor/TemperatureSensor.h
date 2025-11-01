#pragma once

#include <Adafruit_MLX90614.h>

namespace sensor {

class TemperatureSensor {
public:
  bool begin(uint8_t sdaPin, uint8_t sclPin);
  bool read(float &ambientC, float &objectC) const;
  bool ready() const { return ready_; }

private:
  mutable Adafruit_MLX90614 sensor_;
  bool ready_{false};
};

}  // namespace sensor

