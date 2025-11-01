#include "sensor/TemperatureSensor.h"

#include <Wire.h>
#include <math.h>

namespace sensor {

bool TemperatureSensor::begin(uint8_t sdaPin, uint8_t sclPin) {
  Wire.begin(sdaPin, sclPin);
  Wire.setClock(100000);
  ready_ = sensor_.begin();
  return ready_;
}

bool TemperatureSensor::read(float &ambientC, float &objectC) const {
  if (!ready_) {
    return false;
  }

  const float ambient = sensor_.readAmbientTempC();
  const float object = sensor_.readObjectTempC();
  if (isnan(ambient) || isnan(object)) {
    return false;
  }

  ambientC = ambient;
  objectC = object;
  return true;
}

}  // namespace sensor

