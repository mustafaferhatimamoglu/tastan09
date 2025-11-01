#pragma once

#include <Arduino.h>

#include "protection/ProtectionSettings.h"
#include "sensor/MeasurementAggregator.h"

namespace protection {

class ProtectionController {
public:
  using NotificationCallback = void (*)(const String &message);

  explicit ProtectionController(const ProtectionSettings &settings);

  void setNotificationCallback(NotificationCallback callback);
  void initializeHardware();
  void handleProtection(const sensor::MeasurementStats &objectStats, unsigned long now);

  bool heatingActive() const { return heatingRelayState_; }
  bool coolingActive() const { return coolingRelayState_; }

  const ProtectionSettings &settings() const { return settings_; }
  void applySettings(const ProtectionSettings &settings);
  void resetRenotifyTimers(unsigned long now);

  bool setMin(float value, String &errorMessage);
  bool setMax(float value, String &errorMessage);
  bool setHysteresis(float value, String &errorMessage);
  bool setMinSamples(size_t value, String &errorMessage);
  bool setRenotifySeconds(unsigned long seconds, String &errorMessage);

  String formatProtectionConfig() const;
  String formatMeasurementReport(const sensor::MeasurementStats &ambientStats,
                                 const sensor::MeasurementStats &objectStats) const;

private:
  void notify(const String &message) const;
  void writeRelay(uint8_t pin, uint8_t activeLevel, bool enabled) const;

  ProtectionSettings settings_;
  bool heatingRelayState_{false};
  bool coolingRelayState_{false};
  unsigned long lastRelaySwitchMillis_{0};
  unsigned long lastHeatingNotifyMillis_{0};
  unsigned long lastCoolingNotifyMillis_{0};
  NotificationCallback notifyCallback_{nullptr};
};

}  // namespace protection

