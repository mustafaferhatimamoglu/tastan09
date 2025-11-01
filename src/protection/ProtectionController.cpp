#include "protection/ProtectionController.h"

#include <math.h>

#include <Arduino.h>

#include "config.h"

namespace protection {
namespace {
uint8_t inactiveLevel(uint8_t activeLevel) {
  return activeLevel == HIGH ? LOW : HIGH;
}
}

ProtectionController::ProtectionController(const ProtectionSettings &settings) : settings_(settings) {}

void ProtectionController::setNotificationCallback(NotificationCallback callback) {
  notifyCallback_ = callback;
}

void ProtectionController::initializeHardware() {
  if (!config::ENABLE_PROTECTION) {
    return;
  }

  pinMode(config::HEATING_RELAY_PIN, OUTPUT);
  pinMode(config::COOLING_RELAY_PIN, OUTPUT);
  digitalWrite(config::HEATING_RELAY_PIN, inactiveLevel(config::HEATING_RELAY_ACTIVE_LEVEL));
  digitalWrite(config::COOLING_RELAY_PIN, inactiveLevel(config::COOLING_RELAY_ACTIVE_LEVEL));
  heatingRelayState_ = false;
  coolingRelayState_ = false;
  lastRelaySwitchMillis_ = 0;
  lastHeatingNotifyMillis_ = 0;
  lastCoolingNotifyMillis_ = 0;
}

void ProtectionController::applySettings(const ProtectionSettings &settings) {
  settings_ = settings;
}

void ProtectionController::resetRenotifyTimers(unsigned long now) {
  lastHeatingNotifyMillis_ = now;
  lastCoolingNotifyMillis_ = now;
}

bool ProtectionController::setMin(float value, String &errorMessage) {
  if (value >= settings_.maxC) {
    errorMessage = F("Min degeri maksimumdan kucuk olmali.");
    return false;
  }
  settings_.minC = value;
  return true;
}

bool ProtectionController::setMax(float value, String &errorMessage) {
  if (value <= settings_.minC) {
    errorMessage = F("Max degeri minimumdan buyuk olmali.");
    return false;
  }
  settings_.maxC = value;
  return true;
}

bool ProtectionController::setHysteresis(float value, String &errorMessage) {
  const float span = settings_.maxC - settings_.minC;
  if (value <= 0.0f || value >= span) {
    errorMessage = F("Histerezis pozitif olmali ve araligin tamamindan kucuk olmali.");
    return false;
  }
  settings_.hysteresisC = value;
  return true;
}

bool ProtectionController::setMinSamples(size_t value, String &errorMessage) {
  if (value < 1 || value > 3600) {
    errorMessage = F("minSamples 1 ile 3600 arasinda olmali.");
    return false;
  }
  settings_.minSamples = value;
  return true;
}

bool ProtectionController::setRenotifySeconds(unsigned long seconds, String &errorMessage) {
  if (seconds < 10 || seconds > 86400) {
    errorMessage = F("renotify 10 ile 86400 saniye arasinda olmali.");
    return false;
  }
  settings_.renotifyIntervalMs = seconds * 1000UL;
  return true;
}

void ProtectionController::handleProtection(const sensor::MeasurementStats &objectStats, unsigned long now) {
  if (!config::ENABLE_PROTECTION) {
    return;
  }

  const size_t sampleCount = objectStats.count;
  if (sampleCount == 0) {
    return;
  }

  const bool relayActive = heatingRelayState_ || coolingRelayState_;
  if (!relayActive && sampleCount < settings_.minSamples) {
    return;
  }

  const float average = objectStats.average;
  const float current = objectStats.last;
  const float lower = settings_.minC;
  const float upper = settings_.maxC;
  const float hysteresis = settings_.hysteresisC;
  const float mid = (lower + upper) * 0.5f;

  bool desiredHeating = heatingRelayState_;
  bool desiredCooling = coolingRelayState_;

  if (heatingRelayState_) {
    desiredHeating = current < (lower + hysteresis);
  } else {
    desiredHeating = current <= lower;
  }

  if (coolingRelayState_) {
    desiredCooling = current > (upper - hysteresis);
  } else {
    desiredCooling = current >= upper;
  }

  const bool nearCenter = (current > lower && current < upper && fabsf(current - mid) <= hysteresis);
  if ((heatingRelayState_ || coolingRelayState_) && nearCenter) {
    desiredHeating = false;
    desiredCooling = false;
  }

  if (desiredHeating && desiredCooling) {
    if (current <= lower) {
      desiredCooling = false;
    } else if (current >= upper) {
      desiredHeating = false;
    } else {
      desiredHeating = false;
      desiredCooling = false;
    }
  }

  const bool stateChanged = (desiredHeating != heatingRelayState_) || (desiredCooling != coolingRelayState_);
  if (stateChanged) {
    const bool switchingTooFast =
        lastRelaySwitchMillis_ != 0 && (now - lastRelaySwitchMillis_) < config::RELAY_MIN_SWITCH_INTERVAL_MS;
    if (switchingTooFast) {
      return;
    }

    heatingRelayState_ = desiredHeating;
    coolingRelayState_ = desiredCooling;
    writeRelay(config::HEATING_RELAY_PIN, config::HEATING_RELAY_ACTIVE_LEVEL, heatingRelayState_);
    writeRelay(config::COOLING_RELAY_PIN, config::COOLING_RELAY_ACTIVE_LEVEL, coolingRelayState_);
    lastRelaySwitchMillis_ = now;

    if (heatingRelayState_) {
      String message = F("UYARI: Nesne sicakligi alt sinirin altinda. Son: ");
      message += String(current, 2);
      message += F(" C (< ");
      message += String(lower, 2);
      message += F(" C). Ortalama: ");
      message += String(average, 2);
      message += F(" C. Isitma baslatiliyor.");
      notify(message);
      lastHeatingNotifyMillis_ = now;
    } else if (coolingRelayState_) {
      String message = F("UYARI: Nesne sicakligi ust sinirin ustunde. Son: ");
      message += String(current, 2);
      message += F(" C (> ");
      message += String(upper, 2);
      message += F(" C). Ortalama: ");
      message += String(average, 2);
      message += F(" C. Sogutma baslatiliyor.");
      notify(message);
      lastCoolingNotifyMillis_ = now;
    } else {
      String message = F("Bilgi: Nesne sicakligi guvenli araliga dondu. Son: ");
      message += String(current, 2);
      message += F(" C, ortalama: ");
      message += String(average, 2);
      message += F(" C. Koruma devre disi.");
      notify(message);
      lastHeatingNotifyMillis_ = now;
      lastCoolingNotifyMillis_ = now;
    }
  } else {
    if (heatingRelayState_ && (now - lastHeatingNotifyMillis_) >= settings_.renotifyIntervalMs) {
      String message = F("Bilgi: Isitma koruma modu suruyor. Son olcum: ");
      message += String(current, 2);
      message += F(" C (< ");
      message += String(lower, 2);
      message += F(" C). Ortalama: ");
      message += String(average, 2);
      message += F(" C.");
      notify(message);
      lastHeatingNotifyMillis_ = now;
    }
    if (coolingRelayState_ && (now - lastCoolingNotifyMillis_) >= settings_.renotifyIntervalMs) {
      String message = F("Bilgi: Sogutma koruma modu suruyor. Son olcum: ");
      message += String(current, 2);
      message += F(" C (> ");
      message += String(upper, 2);
      message += F(" C). Ortalama: ");
      message += String(average, 2);
      message += F(" C.");
      notify(message);
      lastCoolingNotifyMillis_ = now;
    }

    if (!heatingRelayState_ && !coolingRelayState_ && sampleCount >= settings_.minSamples) {
      lastHeatingNotifyMillis_ = now;
      lastCoolingNotifyMillis_ = now;
    }
  }
}

String ProtectionController::formatProtectionConfig() const {
  String message;
  message.reserve(320);
  message += F("Koruma Ayarlari\n");
  message += F("- min: ");
  message += String(settings_.minC, 2);
  message += F(" C\n- max: ");
  message += String(settings_.maxC, 2);
  message += F(" C\n- histerezis: ");
  message += String(settings_.hysteresisC, 2);
  message += F(" C\n- min ornek sayisi: ");
  message += static_cast<unsigned long>(settings_.minSamples);
  message += F("\n- renotify: ");
  message += settings_.renotifyIntervalMs / 1000UL;
  message += F(" sn\n\nKomutlar:\n");
  message += F("config\n");
  message += F("set min <deger_C>\n");
  message += F("set max <deger_C>\n");
  message += F("set hysteresis <deger_C>\n");
  message += F("set minsamples <tam_sayi>\n");
  message += F("set renotify <saniye>\n");
  message += F("\nNot: min < max olmali, histerezis pozitif ve aralik icinde olmalidir. Tum degisiklikler EEPROM'a kaydedilir.");
  return message;
}

String ProtectionController::formatMeasurementReport(const sensor::MeasurementStats &ambientStats,
                                                     const sensor::MeasurementStats &objectStats) const {
  if (objectStats.count == 0 || ambientStats.count == 0) {
    return String(config::TELEGRAM_NO_DATA_MESSAGE);
  }

  String message;
  message.reserve(320);
  message += F("Olcum Raporu\n");
  message += F("Ornek sayisi: ");
  message += static_cast<unsigned long>(objectStats.count);
  message += F("\nNesne (C)\n");
  message += F("  Ortalama: ");
  message += String(objectStats.average, 2);
  message += F("\n  Min: ");
  message += String(objectStats.min, 2);
  message += F("\n  Maks: ");
  message += String(objectStats.max, 2);
  message += F("\n  Son: ");
  message += String(objectStats.last, 2);
  message += F("\nOrtam (C)\n");
  message += F("  Ortalama: ");
  message += String(ambientStats.average, 2);
  message += F("\n  Min: ");
  message += String(ambientStats.min, 2);
  message += F("\n  Maks: ");
  message += String(ambientStats.max, 2);
  message += F("\n  Son: ");
  message += String(ambientStats.last, 2);
  message += F("\nKoruma: ");
  if (!config::ENABLE_PROTECTION) {
    message += F("Devre disi");
  } else if (heatingRelayState_) {
    message += F("Isitma aktif");
  } else if (coolingRelayState_) {
    message += F("Sogutma aktif");
  } else {
    message += F("Normal");
  }
  message += F("\nSinirlar: ");
  message += String(settings_.minC, 2);
  message += F(" - ");
  message += String(settings_.maxC, 2);
  message += F(" C, histerezis: ");
  message += String(settings_.hysteresisC, 2);
  message += F(" C");
  return message;
}

void ProtectionController::notify(const String &message) const {
  if (notifyCallback_) {
    notifyCallback_(message);
  } else {
    Serial.println(message);
  }
}

void ProtectionController::writeRelay(uint8_t pin, uint8_t activeLevel, bool enabled) const {
  const uint8_t level = enabled ? activeLevel : inactiveLevel(activeLevel);
  digitalWrite(pin, level);
}

}  // namespace protection

