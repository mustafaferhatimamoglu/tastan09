#include "protection/ProtectionStorage.h"

#include <Arduino.h>

#include "protection/ProtectionSettings.h"

namespace protection {
namespace {
constexpr size_t EEPROM_STORAGE_SIZE = 128;
constexpr uint32_t SETTINGS_SIGNATURE = 0x5450524F;  // 'TPRO'
constexpr uint16_t SETTINGS_VERSION = 1;

struct StoredProtectionSettings {
  uint32_t signature;
  uint16_t version;
  uint16_t reserved;
  float minC;
  float maxC;
  float hysteresisC;
  uint16_t minSamples;
  uint32_t renotifyMs;
  uint32_t checksum;
};

uint32_t calculateChecksum(const StoredProtectionSettings &record) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
  const size_t length = sizeof(record) - sizeof(record.checksum);
  uint32_t sum = 0;
  for (size_t i = 0; i < length; ++i) {
    sum = (sum << 1) ^ bytes[i];
  }
  return sum;
}

StoredProtectionSettings buildRecord(const ProtectionSettings &settings) {
  StoredProtectionSettings record{};
  record.signature = SETTINGS_SIGNATURE;
  record.version = SETTINGS_VERSION;
  record.reserved = 0;
  record.minC = settings.minC;
  record.maxC = settings.maxC;
  record.hysteresisC = settings.hysteresisC;
  record.minSamples = static_cast<uint16_t>(settings.minSamples);
  record.renotifyMs = static_cast<uint32_t>(settings.renotifyIntervalMs);
  record.checksum = calculateChecksum(record);
  return record;
}

bool beginEeprom(size_t size) {
#if defined(ESP8266)
  EEPROM.begin(size);
  return true;
#else
  return EEPROM.begin(size);
#endif
}

}  // namespace

bool ProtectionSettingsStorage::init() {
  if (initialized_) {
    return true;
  }
  if (!beginEeprom(EEPROM_STORAGE_SIZE)) {
    Serial.println(F("EEPROM baslatilamadi"));
    return false;
  }
  initialized_ = true;
  return true;
}

bool ProtectionSettingsStorage::load(ProtectionSettings &settings) {
  if (!init()) {
    return false;
  }

  StoredProtectionSettings record{};
  EEPROM.get(0, record);
  if (record.signature != SETTINGS_SIGNATURE || record.version != SETTINGS_VERSION) {
    return false;
  }

  const uint32_t expectedChecksum = calculateChecksum(record);
  if (record.checksum != expectedChecksum) {
    Serial.println(F("EEPROM: koruma ayarlari checksum hatasi"));
    return false;
  }

  ProtectionSettings candidate{
      record.minC,
      record.maxC,
      record.hysteresisC,
      static_cast<size_t>(record.minSamples),
      static_cast<unsigned long>(record.renotifyMs),
  };

  if (!validateProtectionSettings(candidate)) {
    Serial.println(F("EEPROM: koruma ayarlari gecersiz"));
    return false;
  }

  settings = candidate;
  return true;
}

bool ProtectionSettingsStorage::save(const ProtectionSettings &settings) {
  if (!init()) {
    return false;
  }

  StoredProtectionSettings record = buildRecord(settings);
  EEPROM.put(0, record);
  if (!EEPROM.commit()) {
    Serial.println(F("EEPROM: commit basarisiz"));
    return false;
  }
  return true;
}

}  // namespace protection
