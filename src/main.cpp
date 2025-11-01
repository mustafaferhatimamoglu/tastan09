#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "blink/BlinkController.h"
#include "config.h"
#include "protection/ProtectionController.h"
#include "protection/ProtectionStorage.h"
#include "sensor/MeasurementAggregator.h"
#include "sensor/TemperatureSensor.h"
#include "telegram/TelegramCommandProcessor.h"
#include "telegram/TelegramService.h"

namespace {
constexpr uint8_t LED_PIN = LED_BUILTIN;
constexpr uint8_t LED_ACTIVE_LEVEL = LOW;
constexpr uint8_t LED_INACTIVE_LEVEL = HIGH;

blink::BlinkController blinkController;
blink::LedMode activeLedMode = blink::LedMode::Normal;

sensor::TemperatureSensor temperatureSensor;
sensor::MeasurementAggregator ambientAggregator;
sensor::MeasurementAggregator objectAggregator;

protection::ProtectionSettings defaultProtectionSettings{
    config::OBJECT_TEMP_MIN_C,
    config::OBJECT_TEMP_MAX_C,
    config::OBJECT_TEMP_HYSTERESIS_C,
    config::PROTECTION_MIN_SAMPLES,
    config::PROTECTION_RENOTIFY_INTERVAL_MS,
};

protection::ProtectionController protectionController(defaultProtectionSettings);
protection::ProtectionSettingsStorage protectionStorage;

telegram::TelegramService telegramService;
telegram::TelegramCommandProcessor commandProcessor(protectionController, protectionStorage, telegramService);

unsigned long lastTelegramReport = 0;
telegram::TelegramService *globalTelegramService = nullptr;

void setLedMode(blink::LedMode mode) {
  blinkController.setMode(mode);
  activeLedMode = mode;
}

void notifyProtectionEvent(const String &message) {
  Serial.println(message);
  if (globalTelegramService) {
    globalTelegramService->sendAlert(message);
  }
}

bool connectToWifi() {
  if (strlen(config::WIFI_SSID) == 0) {
    Serial.println(F("Wi-Fi SSID bos. config.h dosyasini guncelleyin."));
    setLedMode(blink::LedMode::WifiError);
    return false;
  }

  setLedMode(blink::LedMode::WifiConnecting);
  WiFi.mode(WIFI_STA);
  WiFi.begin(config::WIFI_SSID, config::WIFI_PASSWORD);

  Serial.print(F("Wi-Fi baglaniliyor"));
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < config::WIFI_CONNECT_TIMEOUT_MS) {
    blinkController.update();
    delay(10);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F(" [BASARILI]"));
    setLedMode(blink::LedMode::Normal);
    telegramService.trySendStartupMessage();
    return true;
  }

  Serial.println(F(" [HATA]"));
  setLedMode(blink::LedMode::WifiError);
  return false;
}

void maybeProcessMeasurement(unsigned long now) {
  static unsigned long lastMeasurementAttempt = 0;
  if (!config::ENABLE_DATA_FETCH) {
    return;
  }
  if (!temperatureSensor.ready()) {
    return;
  }
  if (now - lastMeasurementAttempt < config::MEASUREMENT_INTERVAL_MS) {
    return;
  }
  lastMeasurementAttempt = now;

  float ambientC = 0.0f;
  float objectC = 0.0f;
  if (!temperatureSensor.read(ambientC, objectC)) {
    Serial.println(F("Olcum alinamadi"));
    setLedMode(blink::LedMode::DataError);
    return;
  }

  ambientAggregator.addSample(ambientC);
  objectAggregator.addSample(objectC);
  Serial.print(F("MLX90614 -> Nesne: "));
  Serial.print(objectC, 2);
  Serial.print(F(" C, Ortam: "));
  Serial.print(ambientC, 2);
  Serial.println(F(" C"));

  if (activeLedMode == blink::LedMode::DataError) {
    setLedMode(blink::LedMode::Normal);
  }

  const sensor::MeasurementStats objectStats = objectAggregator.stats();
  protectionController.handleProtection(objectStats, now);
}

void maybeSendTelegramReport(unsigned long now) {
  if (!telegramService.configured() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (now - lastTelegramReport < config::TELEGRAM_REPORT_INTERVAL_MS) {
    return;
  }
  lastTelegramReport = now;

  if (!objectAggregator.hasSamples() || !ambientAggregator.hasSamples()) {
    if (config::ENABLE_DATA_FETCH) {
      telegramService.sendInfo(String(config::TELEGRAM_NO_DATA_MESSAGE));
    }
    return;
  }

  const sensor::MeasurementStats ambientStats = ambientAggregator.stats();
  const sensor::MeasurementStats objectStats = objectAggregator.stats();
  const String message = protectionController.formatMeasurementReport(ambientStats, objectStats);
  if (telegramService.sendInfo(message)) {
    ambientAggregator.reset();
    objectAggregator.reset();
  }
}

void initializeProtectionHardware() {
  protectionController.initializeHardware();
  protectionController.setNotificationCallback(notifyProtectionEvent);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  blinkController.begin(LED_PIN, LED_ACTIVE_LEVEL, LED_INACTIVE_LEVEL);
  setLedMode(blink::LedMode::Normal);

  protection::ProtectionSettings storedSettings = protectionController.settings();
  if (protectionStorage.load(storedSettings)) {
    protectionController.applySettings(storedSettings);
    Serial.println(F("EEPROM: koruma ayarlari yuklendi."));
  } else if (protectionStorage.save(protectionController.settings())) {
    Serial.println(F("EEPROM: varsayilan koruma ayarlari kaydedildi."));
  }

  if (config::ENABLE_DATA_FETCH) {
    if (temperatureSensor.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN)) {
      Serial.println(F("MLX90614 hazir"));
    } else {
      Serial.println(F("MLX90614 baslatilamadi"));
      setLedMode(blink::LedMode::DataError);
    }
  }

  initializeProtectionHardware();
  globalTelegramService = &telegramService;

  connectToWifi();
}

void loop() {
  blinkController.update();
  const unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastRetry = 0;
    if (now - lastRetry >= config::WIFI_RETRY_INTERVAL_MS) {
      lastRetry = now;
      connectToWifi();
    }
    delay(10);
    return;
  }

  telegramService.trySendStartupMessage();
  maybeProcessMeasurement(now);
  maybeSendTelegramReport(now);
  telegramService.pollUpdates(now, commandProcessor, objectAggregator.stats());

  if (activeLedMode != blink::LedMode::DataError && activeLedMode != blink::LedMode::Normal) {
    setLedMode(blink::LedMode::Normal);
  }

  delay(10);
}

