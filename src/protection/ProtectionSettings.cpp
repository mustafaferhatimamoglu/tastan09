#include "protection/ProtectionSettings.h"

namespace protection {

bool validateProtectionSettings(const ProtectionSettings &settings) {
  if (settings.minC >= settings.maxC) {
    return false;
  }
  const float span = settings.maxC - settings.minC;
  if (settings.hysteresisC <= 0.0f || settings.hysteresisC >= span) {
    return false;
  }
  if (settings.minSamples < 1 || settings.minSamples > 3600) {
    return false;
  }
  if (settings.renotifyIntervalMs < 10000UL || settings.renotifyIntervalMs > 86400000UL) {
    return false;
  }
  return true;
}

}  // namespace protection

