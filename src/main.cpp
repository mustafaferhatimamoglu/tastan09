#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <ArduinoJson.h>
#include <math.h>
#include "config.h"

constexpr uint8_t LED_PIN = LED_BUILTIN;
constexpr uint8_t LED_ACTIVE_LEVEL = LOW;
constexpr uint8_t LED_INACTIVE_LEVEL = HIGH;
constexpr unsigned long TELEGRAM_POLL_INTERVAL_MS = 2000;

struct Segment {
  uint16_t duration_ms;
  bool led_on;
};

struct BlinkPattern {
  const Segment *segments;
  size_t length;
};

constexpr uint16_t SHORT_PULSE_MS = 200;
constexpr uint16_t LONG_PULSE_MS = 600;
constexpr uint16_t PATTERN_PAUSE_MS = 1200;

constexpr Segment NORMAL_SEGMENTS[] = {
  {1500, true},
  {1500, false},
};

constexpr Segment WIFI_CONNECTING_SEGMENTS[] = {
  {SHORT_PULSE_MS, true},
  {SHORT_PULSE_MS, false},
  {SHORT_PULSE_MS, true},
  {LONG_PULSE_MS, false},
};

constexpr Segment WIFI_ERROR_SEGMENTS[] = {
  {SHORT_PULSE_MS, true},
  {SHORT_PULSE_MS, false},
  {SHORT_PULSE_MS, true},
  {SHORT_PULSE_MS, false},
  {LONG_PULSE_MS, true},
  {LONG_PULSE_MS, false},
  {LONG_PULSE_MS, true},
  {LONG_PULSE_MS, false},
  {PATTERN_PAUSE_MS, false},
};

constexpr Segment DATA_ERROR_SEGMENTS[] = {
  {SHORT_PULSE_MS, true},
  {SHORT_PULSE_MS, false},
  {LONG_PULSE_MS, true},
  {LONG_PULSE_MS, false},
  {LONG_PULSE_MS, true},
  {LONG_PULSE_MS, false},
  {3000, false},
};

constexpr BlinkPattern NORMAL_PATTERN{NORMAL_SEGMENTS, sizeof(NORMAL_SEGMENTS) / sizeof(Segment)};
constexpr BlinkPattern WIFI_CONNECTING_PATTERN{WIFI_CONNECTING_SEGMENTS, sizeof(WIFI_CONNECTING_SEGMENTS) / sizeof(Segment)};
constexpr BlinkPattern WIFI_ERROR_PATTERN{WIFI_ERROR_SEGMENTS, sizeof(WIFI_ERROR_SEGMENTS) / sizeof(Segment)};
constexpr BlinkPattern DATA_ERROR_PATTERN{DATA_ERROR_SEGMENTS, sizeof(DATA_ERROR_SEGMENTS) / sizeof(Segment)};

class BlinkController {
public:
  void begin(uint8_t pin, uint8_t activeLevel, uint8_t inactiveLevel) {
    pin_ = pin;
    activeLevel_ = activeLevel;
    inactiveLevel_ = inactiveLevel;
    pinMode(pin_, OUTPUT);
    digitalWrite(pin_, inactiveLevel_);
  }

  void setPattern(const BlinkPattern &pattern) {
    if (pattern_ == &pattern) {
      return;
    }
    pattern_ = &pattern;
    patternIndex_ = 0;
    applyCurrentSegment();
    lastTransition_ = millis();
  }

  void update() {
    if (!pattern_ || pattern_->length == 0) {
      return;
    }

    const Segment &activeSegment = pattern_->segments[patternIndex_];
    const unsigned long now = millis();
    if (now - lastTransition_ < activeSegment.duration_ms) {
      return;
    }

    patternIndex_ = (patternIndex_ + 1) % pattern_->length;
    applyCurrentSegment();
    lastTransition_ = now;
  }

private:
  void applyCurrentSegment() {
    if (!pattern_ || pattern_->length == 0) {
      return;
    }
    const Segment &segment = pattern_->segments[patternIndex_];
    digitalWrite(pin_, segment.led_on ? activeLevel_ : inactiveLevel_);
  }

  uint8_t pin_{0};
  uint8_t activeLevel_{LED_ACTIVE_LEVEL};
  uint8_t inactiveLevel_{LED_INACTIVE_LEVEL};
  const BlinkPattern *pattern_{nullptr};
  size_t patternIndex_{0};
  unsigned long lastTransition_{0};
};

enum class LedMode : uint8_t {
  Normal,
  WifiConnecting,
  WifiError,
  DataError,
};

const BlinkPattern &patternForMode(LedMode mode) {
  switch (mode) {
    case LedMode::WifiConnecting:
      return WIFI_CONNECTING_PATTERN;
    case LedMode::WifiError:
      return WIFI_ERROR_PATTERN;
    case LedMode::DataError:
      return DATA_ERROR_PATTERN;
    case LedMode::Normal:
    default:
      return NORMAL_PATTERN;
  }
}

BlinkController blinkController;
LedMode currentMode = LedMode::Normal;
bool patternInitialized = false;

void setLedMode(LedMode mode) {
  if (patternInitialized && mode == currentMode) {
    return;
  }
  currentMode = mode;
  patternInitialized = true;
  blinkController.setPattern(patternForMode(mode));
}

struct MeasurementStats {
  float min = 0.0f;
  float max = 0.0f;
  float average = 0.0f;
  float last = 0.0f;
  size_t count = 0;
};

class MeasurementAggregator {
public:
  void reset() {
    min_ = 0.0f;
    max_ = 0.0f;
    sum_ = 0.0f;
    last_ = 0.0f;
    count_ = 0;
  }

  void addSample(float value) {
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

  bool hasSamples() const {
    return count_ > 0;
  }

  MeasurementStats stats() const {
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

private:
  float min_{0.0f};
  float max_{0.0f};
  float sum_{0.0f};
  float last_{0.0f};
  size_t count_{0};
};

struct ProtectionSettings {
  float minC;
  float maxC;
  float hysteresisC;
  size_t minSamples;
  unsigned long renotifyIntervalMs;
};

Adafruit_MLX90614 mlx90614;
bool mlxReady = false;
MeasurementAggregator ambientAggregator;
MeasurementAggregator objectAggregator;
ProtectionSettings protectionSettings{
  config::OBJECT_TEMP_MIN_C,
  config::OBJECT_TEMP_MAX_C,
  config::OBJECT_TEMP_HYSTERESIS_C,
  config::PROTECTION_MIN_SAMPLES,
  config::PROTECTION_RENOTIFY_INTERVAL_MS
};
bool startupMessageSent = false;
unsigned long lastTelegramReport = 0;
bool heatingRelayState = false;
bool coolingRelayState = false;
unsigned long lastRelaySwitchMillis = 0;
unsigned long lastHeatingNotifyMillis = 0;
unsigned long lastCoolingNotifyMillis = 0;
const String TELEGRAM_ALERT_CHAT_ID_STR = String(config::TELEGRAM_ALERT_CHAT_ID);
const String TELEGRAM_INFO_CHAT_ID_STR = String(config::TELEGRAM_INFO_CHAT_ID);
long telegramLastUpdateId = 0;
unsigned long lastTelegramPoll = 0;

bool telegramConfigured();
String urlEncode(const String &value);
bool sendTelegramMessage(const String &text);
bool sendTelegramMessage(const String &text, const String &chatId);
bool sendInfoTelegramMessage(const String &text);
void notifyProtectionEvent(const String &message);
String formatProtectionConfig();
void processTelegramCommand(const String &text, const String &chatId);
void pollTelegramUpdates(unsigned long now);
bool readMeasurement(float &ambientC, float &objectC);
void handleProtection(const MeasurementStats &objectStats, unsigned long now);
void maybeProcessMeasurement(unsigned long now);
void maybeSendTelegramReport(unsigned long now);
bool connectToWifi();
void initializeProtectionHardware();

bool telegramConfigured() {
  return config::ENABLE_TELEGRAM && strlen(config::TELEGRAM_BOT_TOKEN) > 0;
}

String urlEncode(const String &value) {
  String encoded;
  encoded.reserve(value.length() * 3);
  const char hex[] = "0123456789ABCDEF";

  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else if (c == ' ') {
      encoded += '+';
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

bool sendTelegramMessage(const String &text, const String &chatId) {
  if (!telegramConfigured()) {
    return false;
  }
  if (chatId.length() == 0) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClientSecure client;
  if (config::TELEGRAM_ALLOW_INSECURE_TLS) {
    client.setInsecure();
  }

  HTTPClient https;
  const String url = String(F("https://api.telegram.org/bot")) + config::TELEGRAM_BOT_TOKEN + F("/sendMessage");
  if (!https.begin(client, url)) {
    Serial.println(F("Telegram: baglanti kurulamadi"));
    return false;
  }

  https.addHeader(F("Content-Type"), F("application/x-www-form-urlencoded"));
  const String payload = String(F("chat_id=")) + chatId + F("&text=") + urlEncode(text);
  const int httpCode = https.POST(payload);
  if (httpCode < 200 || httpCode >= 300) {
    Serial.print(F("Telegram HTTP hatasi: "));
    Serial.println(httpCode);
    https.end();
    return false;
  }

  https.end();
  Serial.println(F("Telegram mesaji gonderildi"));
  return true;
}

bool sendTelegramMessage(const String &text) {
  if (TELEGRAM_ALERT_CHAT_ID_STR.length() > 0) {
    return sendTelegramMessage(text, TELEGRAM_ALERT_CHAT_ID_STR);
  }
  if (TELEGRAM_INFO_CHAT_ID_STR.length() > 0) {
    return sendTelegramMessage(text, TELEGRAM_INFO_CHAT_ID_STR);
  }
  return false;
}

bool sendInfoTelegramMessage(const String &text) {
  if (TELEGRAM_INFO_CHAT_ID_STR.length() > 0) {
    return sendTelegramMessage(text, TELEGRAM_INFO_CHAT_ID_STR);
  }
  return sendTelegramMessage(text);
}

uint8_t inactiveLevel(uint8_t activeLevel) {
  return activeLevel == HIGH ? LOW : HIGH;
}

void writeRelay(uint8_t pin, uint8_t activeLevel, bool enabled) {
  const uint8_t level = enabled ? activeLevel : inactiveLevel(activeLevel);
  digitalWrite(pin, level);
}

void notifyProtectionEvent(const String &message) {
  Serial.println(message);
  sendTelegramMessage(message);
}

void trySendStartupMessage() {
  if (startupMessageSent || !telegramConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (sendInfoTelegramMessage(String(config::TELEGRAM_START_MESSAGE))) {
    startupMessageSent = true;
  }
}

String formatProtectionConfig() {
  String message;
  message.reserve(320);
  message += F("Koruma Ayarlari\n");
  message += F("- min: ");
  message += String(protectionSettings.minC, 2);
  message += F(" C\n- max: ");
  message += String(protectionSettings.maxC, 2);
  message += F(" C\n- histerezis: ");
  message += String(protectionSettings.hysteresisC, 2);
  message += F(" C\n- min ornek sayisi: ");
  message += static_cast<unsigned long>(protectionSettings.minSamples);
  message += F("\n- renotify: ");
  message += protectionSettings.renotifyIntervalMs / 1000UL;
  message += F(" sn\n\nKomutlar:\n");
  message += F("config\n");
  message += F("set min <deger_C>\n");
  message += F("set max <deger_C>\n");
  message += F("set hysteresis <deger_C>\n");
  message += F("set minsamples <tam_sayi>\n");
  message += F("set renotify <saniye>\n");
  message += F("\nNot: min < max olmali, histerezis pozitif ve aralik icinde olmalidir.");
  return message;
}

String formatMeasurementReport(const MeasurementStats &ambientStats, const MeasurementStats &objectStats) {
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
  } else if (heatingRelayState) {
    message += F("Isitma aktif");
  } else if (coolingRelayState) {
    message += F("Sogutma aktif");
  } else {
    message += F("Normal");
  }
  message += F("\nSinirlar: ");
  message += String(protectionSettings.minC, 2);
  message += F(" - ");
  message += String(protectionSettings.maxC, 2);
  message += F(" C, histerezis: ");
  message += String(protectionSettings.hysteresisC, 2);
  message += F(" C");
  return message;
}

bool readMeasurement(float &ambientC, float &objectC) {
  if (!mlxReady) {
    return false;
  }

  const float ambient = mlx90614.readAmbientTempC();
  const float object = mlx90614.readObjectTempC();
  if (isnan(ambient) || isnan(object)) {
    return false;
  }

  ambientC = ambient;
  objectC = object;
  return true;
}

bool isValidNumber(const String &value, bool allowDecimal) {
  if (value.length() == 0) {
    return false;
  }
  bool seenDigit = false;
  bool seenDecimal = false;
  size_t start = (value[0] == '-' ? 1 : 0);
  for (size_t i = start; i < value.length(); ++i) {
    const char c = value[i];
    if (c >= '0' && c <= '9') {
      seenDigit = true;
      continue;
    }
    if (allowDecimal && c == '.' && !seenDecimal) {
      seenDecimal = true;
      continue;
    }
    return false;
  }
  return seenDigit;
}

bool isAuthorizedChat(const String &chatId) {
  if (chatId.length() == 0) {
    return false;
  }
  if (TELEGRAM_ALERT_CHAT_ID_STR.length() > 0 && chatId == TELEGRAM_ALERT_CHAT_ID_STR) {
    return true;
  }
  if (TELEGRAM_INFO_CHAT_ID_STR.length() > 0 && chatId == TELEGRAM_INFO_CHAT_ID_STR) {
    return true;
  }
  if (TELEGRAM_ALERT_CHAT_ID_STR.length() == 0 && TELEGRAM_INFO_CHAT_ID_STR.length() == 0) {
    return true;
  }
  return false;
}

void processTelegramCommand(const String &text, const String &chatId) {
  String trimmed = text;
  trimmed.trim();
  if (trimmed.length() == 0) {
    return;
  }

  String lower = trimmed;
  lower.toLowerCase();
  const unsigned long now = millis();

  if (lower == F("config")) {
    sendTelegramMessage(formatProtectionConfig(), chatId);
    return;
  }

  if (!lower.startsWith(F("set "))) {
    sendTelegramMessage(F("Bilinmeyen komut. 'config' veya 'set ...' kullanin."), chatId);
    return;
  }

  const int keyStart = 4;
  const int spaceIndex = lower.indexOf(' ', keyStart);
  if (spaceIndex < 0) {
    sendTelegramMessage(F("Eksik parametre. Ornek: set min 22.5"), chatId);
    return;
  }

  const String key = lower.substring(keyStart, spaceIndex);
  String valueText = trimmed.substring(spaceIndex + 1);
  valueText.trim();
  if (valueText.length() == 0) {
    sendTelegramMessage(F("Deger bulunamadi. Ornek: set max 28.0"), chatId);
    return;
  }

  bool updated = false;
  String response;

  if (key == F("min")) {
    if (!isValidNumber(valueText, true)) {
      sendTelegramMessage(F("Gecersiz sayi. Ondalik icin nokta kullanin."), chatId);
      return;
    }
    const float newMin = valueText.toFloat();
    if (newMin >= protectionSettings.maxC) {
      sendTelegramMessage(F("Min degeri maksimumdan kucuk olmali."), chatId);
      return;
    }
    protectionSettings.minC = newMin;
    response = F("Ayar guncellendi: min = ");
    response += String(newMin, 2);
    response += F(" C");
    updated = true;
  } else if (key == F("max")) {
    if (!isValidNumber(valueText, true)) {
      sendTelegramMessage(F("Gecersiz sayi. Ondalik icin nokta kullanin."), chatId);
      return;
    }
    const float newMax = valueText.toFloat();
    if (newMax <= protectionSettings.minC) {
      sendTelegramMessage(F("Max degeri minimumdan buyuk olmali."), chatId);
      return;
    }
    protectionSettings.maxC = newMax;
    response = F("Ayar guncellendi: max = ");
    response += String(newMax, 2);
    response += F(" C");
    updated = true;
  } else if (key == F("hysteresis")) {
    if (!isValidNumber(valueText, true)) {
      sendTelegramMessage(F("Gecersiz sayi. Ondalik icin nokta kullanin."), chatId);
      return;
    }
    const float newH = valueText.toFloat();
    const float span = protectionSettings.maxC - protectionSettings.minC;
    if (newH <= 0.0f || newH >= span) {
      sendTelegramMessage(F("Histerezis pozitif olmali ve araligin tamamindan kucuk olmali."), chatId);
      return;
    }
    protectionSettings.hysteresisC = newH;
    response = F("Ayar guncellendi: histerezis = ");
    response += String(newH, 2);
    response += F(" C");
    updated = true;
  } else if (key == F("minsamples")) {
    if (!isValidNumber(valueText, false)) {
      sendTelegramMessage(F("Gecersiz tam sayi."), chatId);
      return;
    }
    const long newSamples = valueText.toInt();
    if (newSamples < 1 || newSamples > 3600) {
      sendTelegramMessage(F("minSamples 1 ile 3600 arasinda olmali."), chatId);
      return;
    }
    protectionSettings.minSamples = static_cast<size_t>(newSamples);
    response = F("Ayar guncellendi: min ornek sayisi = ");
    response += static_cast<unsigned long>(protectionSettings.minSamples);
    updated = true;
  } else if (key == F("renotify")) {
    if (!isValidNumber(valueText, false)) {
      sendTelegramMessage(F("Gecersiz tam sayi."), chatId);
      return;
    }
    const long seconds = valueText.toInt();
    if (seconds < 10 || seconds > 86400) {
      sendTelegramMessage(F("renotify 10 ile 86400 saniye arasinda olmali."), chatId);
      return;
    }
    protectionSettings.renotifyIntervalMs = static_cast<unsigned long>(seconds) * 1000UL;
    response = F("Ayar guncellendi: renotify = ");
    response += seconds;
    response += F(" sn");
    updated = true;
  } else {
    sendTelegramMessage(F("Bilinmeyen ayar anahtari. 'config' yazarak yardim alabilirsiniz."), chatId);
    return;
  }

  if (updated) {
    sendTelegramMessage(response, chatId);
    handleProtection(objectAggregator.stats(), now);
  }
}

void pollTelegramUpdates(unsigned long now) {
  if (!telegramConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (now - lastTelegramPoll < TELEGRAM_POLL_INTERVAL_MS) {
    return;
  }
  lastTelegramPoll = now;

  WiFiClientSecure client;
  if (config::TELEGRAM_ALLOW_INSECURE_TLS) {
    client.setInsecure();
  }

  HTTPClient https;
  String url = String(F("https://api.telegram.org/bot")) + config::TELEGRAM_BOT_TOKEN + F("/getUpdates?timeout=0");
  if (telegramLastUpdateId > 0) {
    url += F("&offset=");
    url += String(telegramLastUpdateId + 1);
  }

  if (!https.begin(client, url)) {
    Serial.println(F("Telegram: getUpdates baslatilamadi"));
    return;
  }

  const int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print(F("Telegram getUpdates HTTP hatasi: "));
    Serial.println(httpCode);
    https.end();
    return;
  }

  const String payload = https.getString();
  https.end();
  if (payload.length() == 0) {
    return;
  }

  DynamicJsonDocument doc(4096);
  const DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print(F("Telegram JSON hatasi: "));
    Serial.println(error.c_str());
    return;
  }

  if (!doc.containsKey("result")) {
    return;
  }

  for (JsonObject update : doc["result"].as<JsonArray>()) {
    const long updateId = update["update_id"] | 0;
    if (updateId <= telegramLastUpdateId) {
      continue;
    }
    telegramLastUpdateId = updateId;

    JsonObject messageObj = update["message"].as<JsonObject>();
    if (messageObj.isNull()) {
      continue;
    }

    const String chatId = messageObj["chat"]["id"].as<String>();
    if (!isAuthorizedChat(chatId)) {
      continue;
    }

    String text = messageObj["text"] | "";
    text.trim();
    if (text.length() == 0) {
      continue;
    }

    processTelegramCommand(text, chatId);
  }
}

void handleProtection(const MeasurementStats &objectStats, unsigned long now) {
  if (!config::ENABLE_PROTECTION) {
    return;
  }
  if (objectStats.count < protectionSettings.minSamples) {
    return;
  }

  const float average = objectStats.average;
  const float lower = protectionSettings.minC;
  const float upper = protectionSettings.maxC;
  const float hysteresis = protectionSettings.hysteresisC;
  const float mid = (lower + upper) * 0.5f;

  bool desiredHeating = heatingRelayState;
  bool desiredCooling = coolingRelayState;

  if (heatingRelayState) {
    desiredHeating = average < (lower + hysteresis);
  } else {
    desiredHeating = average <= lower;
  }

  if (coolingRelayState) {
    desiredCooling = average > (upper - hysteresis);
  } else {
    desiredCooling = average >= upper;
  }

  const bool nearCenter = (average > lower && average < upper && fabsf(average - mid) <= hysteresis);
  if ((heatingRelayState || coolingRelayState) && nearCenter) {
    desiredHeating = false;
    desiredCooling = false;
  }

  if (desiredHeating && desiredCooling) {
    if (average <= lower) {
      desiredCooling = false;
    } else if (average >= upper) {
      desiredHeating = false;
    } else {
      desiredHeating = false;
      desiredCooling = false;
    }
  }

  const bool stateChanged = (desiredHeating != heatingRelayState) || (desiredCooling != coolingRelayState);
  if (stateChanged) {
    const bool switchingTooFast = lastRelaySwitchMillis != 0 && (now - lastRelaySwitchMillis) < config::RELAY_MIN_SWITCH_INTERVAL_MS;
    if (switchingTooFast) {
      return;
    }

    heatingRelayState = desiredHeating;
    coolingRelayState = desiredCooling;
    writeRelay(config::HEATING_RELAY_PIN, config::HEATING_RELAY_ACTIVE_LEVEL, heatingRelayState);
    writeRelay(config::COOLING_RELAY_PIN, config::COOLING_RELAY_ACTIVE_LEVEL, coolingRelayState);
    lastRelaySwitchMillis = now;

    if (heatingRelayState) {
      String message = F("UYARI: Ortalama nesne sicakligi alt sinirin altinda. Ortalama: ");
      message += String(average, 2);
      message += F(" C (< ");
      message += String(lower, 2);
      message += F(" C). Isitma baslatiliyor.");
      notifyProtectionEvent(message);
      lastHeatingNotifyMillis = now;
    } else if (coolingRelayState) {
      String message = F("UYARI: Ortalama nesne sicakligi ust sinirin ustunde. Ortalama: ");
      message += String(average, 2);
      message += F(" C (> ");
      message += String(upper, 2);
      message += F(" C). Sogutma baslatiliyor.");
      notifyProtectionEvent(message);
      lastCoolingNotifyMillis = now;
    } else {
      String message = F("Bilgi: Ortalama nesne sicakligi guvenli araliga dondu. Ortalama: ");
      message += String(average, 2);
      message += F(" C. Koruma devre disi.");
      notifyProtectionEvent(message);
      lastHeatingNotifyMillis = now;
      lastCoolingNotifyMillis = now;
    }
  } else {
    if (heatingRelayState && (now - lastHeatingNotifyMillis) >= protectionSettings.renotifyIntervalMs) {
      String message = F("Bilgi: Isitma koruma modu suruyor. Ortalama nesne sicakligi: ");
      message += String(average, 2);
      message += F(" C (< ");
      message += String(lower, 2);
      message += F(" C).");
      notifyProtectionEvent(message);
      lastHeatingNotifyMillis = now;
    }
    if (coolingRelayState && (now - lastCoolingNotifyMillis) >= protectionSettings.renotifyIntervalMs) {
      String message = F("Bilgi: Sogutma koruma modu suruyor. Ortalama nesne sicakligi: ");
      message += String(average, 2);
      message += F(" C (> ");
      message += String(upper, 2);
      message += F(" C).");
      notifyProtectionEvent(message);
      lastCoolingNotifyMillis = now;
    }

    if (!heatingRelayState && !coolingRelayState && objectStats.count >= protectionSettings.minSamples) {
      lastHeatingNotifyMillis = now;
      lastCoolingNotifyMillis = now;
    }
  }
}

void maybeProcessMeasurement(unsigned long now) {
  static unsigned long lastMeasurementAttempt = 0;
  if (!config::ENABLE_DATA_FETCH) {
    return;
  }

  if (!mlxReady) {
    return;
  }

  if (now - lastMeasurementAttempt < config::MEASUREMENT_INTERVAL_MS) {
    return;
  }
  lastMeasurementAttempt = now;

  float ambientC = 0.0f;
  float objectC = 0.0f;
  if (!readMeasurement(ambientC, objectC)) {
    Serial.println(F("Olcum alinamadi"));
    setLedMode(LedMode::DataError);
    return;
  }

  ambientAggregator.addSample(ambientC);
  objectAggregator.addSample(objectC);
  Serial.print(F("MLX90614 -> Nesne: "));
  Serial.print(objectC, 2);
  Serial.print(F(" C, Ortam: "));
  Serial.print(ambientC, 2);
  Serial.println(F(" C"));
  if (currentMode == LedMode::DataError) {
    setLedMode(LedMode::Normal);
  }

  const MeasurementStats objectStats = objectAggregator.stats();
  handleProtection(objectStats, now);
}

void maybeSendTelegramReport(unsigned long now) {
  if (!telegramConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (now - lastTelegramReport < config::TELEGRAM_REPORT_INTERVAL_MS) {
    return;
  }
  lastTelegramReport = now;

  if (!objectAggregator.hasSamples() || !ambientAggregator.hasSamples()) {
    if (config::ENABLE_DATA_FETCH) {
      sendInfoTelegramMessage(String(config::TELEGRAM_NO_DATA_MESSAGE));
    }
    return;
  }

  const MeasurementStats ambientStats = ambientAggregator.stats();
  const MeasurementStats objectStats = objectAggregator.stats();
  const String message = formatMeasurementReport(ambientStats, objectStats);
  if (sendInfoTelegramMessage(message)) {
    ambientAggregator.reset();
    objectAggregator.reset();
  }
}

bool connectToWifi() {
  if (strlen(config::WIFI_SSID) == 0) {
    Serial.println(F("Wi-Fi SSID bos. config.h dosyasini guncelleyin."));
    setLedMode(LedMode::WifiError);
    return false;
  }

  setLedMode(LedMode::WifiConnecting);
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
    setLedMode(LedMode::Normal);
    trySendStartupMessage();
    return true;
  }

  Serial.println(F(" [HATA]"));
  setLedMode(LedMode::WifiError);
  return false;
}

void initializeProtectionHardware() {
  if (!config::ENABLE_PROTECTION) {
    return;
  }
  pinMode(config::HEATING_RELAY_PIN, OUTPUT);
  pinMode(config::COOLING_RELAY_PIN, OUTPUT);
  heatingRelayState = false;
  coolingRelayState = false;
  writeRelay(config::HEATING_RELAY_PIN, config::HEATING_RELAY_ACTIVE_LEVEL, false);
  writeRelay(config::COOLING_RELAY_PIN, config::COOLING_RELAY_ACTIVE_LEVEL, false);
}

void setup() {
  Serial.begin(115200);
  blinkController.begin(LED_PIN, LED_ACTIVE_LEVEL, LED_INACTIVE_LEVEL);
  setLedMode(LedMode::Normal);

  if (config::ENABLE_DATA_FETCH) {
    Wire.begin(config::I2C_SDA_PIN, config::I2C_SCL_PIN);
    Wire.setClock(100000);
    if (mlx90614.begin()) {
      mlxReady = true;
      Serial.println(F("MLX90614 hazir"));
    } else {
      Serial.println(F("MLX90614 baslatilamadi"));
      setLedMode(LedMode::DataError);
    }
  }

  initializeProtectionHardware();
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

  trySendStartupMessage();
  maybeProcessMeasurement(now);
  maybeSendTelegramReport(now);
  pollTelegramUpdates(now);

  if (currentMode != LedMode::DataError && currentMode != LedMode::Normal) {
    setLedMode(LedMode::Normal);
  }

  delay(10);
}
