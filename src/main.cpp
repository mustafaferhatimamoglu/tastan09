#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"

constexpr uint8_t LED_PIN = LED_BUILTIN;
constexpr uint8_t LED_ACTIVE_LEVEL = LOW;
constexpr uint8_t LED_INACTIVE_LEVEL = HIGH;

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

MeasurementAggregator measurementAggregator;
bool startupMessageSent = false;
unsigned long lastTelegramReport = 0;

bool telegramConfigured() {
  return config::ENABLE_TELEGRAM && strlen(config::TELEGRAM_BOT_TOKEN) > 0 && strlen(config::TELEGRAM_CHAT_ID) > 0;
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

bool sendTelegramMessage(const String &text) {
  if (!telegramConfigured()) {
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
  const String payload = String(F("chat_id=")) + config::TELEGRAM_CHAT_ID + F("&text=") + urlEncode(text);
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

void trySendStartupMessage() {
  if (startupMessageSent || !telegramConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (sendTelegramMessage(String(config::TELEGRAM_START_MESSAGE))) {
    startupMessageSent = true;
  }
}

String formatMeasurementReport(const MeasurementStats &stats) {
  if (stats.count == 0) {
    return String(config::TELEGRAM_NO_DATA_MESSAGE);
  }

  String message;
  message.reserve(160);
  message += F("Olcum Raporu\n");
  message += F("Ornek sayisi: ");
  message += static_cast<unsigned long>(stats.count);
  message += F("\nOrtalama: ");
  message += String(stats.average, 2);
  message += F("\nMin: ");
  message += String(stats.min, 2);
  message += F("\nMaks: ");
  message += String(stats.max, 2);
  message += F("\nSon: ");
  message += String(stats.last, 2);
  return message;
}

bool readMeasurement(float &value) {
  // TODO: Replace with actual sensor acquisition logic. Return true and set value when a measurement is available.
  value = 0.0f;
  return false;
}

void maybeProcessMeasurement(unsigned long now) {
  static unsigned long lastMeasurementAttempt = 0;
  if (!config::ENABLE_DATA_FETCH) {
    return;
  }

  if (now - lastMeasurementAttempt < config::MEASUREMENT_INTERVAL_MS) {
    return;
  }
  lastMeasurementAttempt = now;

  float measurement = 0.0f;
  if (!readMeasurement(measurement)) {
    Serial.println(F("Olcum alinamadi"));
    setLedMode(LedMode::DataError);
    return;
  }

  measurementAggregator.addSample(measurement);
  Serial.print(F("Olcum: "));
  Serial.println(measurement, 2);
  if (currentMode == LedMode::DataError) {
    setLedMode(LedMode::Normal);
  }
}

void maybeSendTelegramReport(unsigned long now) {
  if (!telegramConfigured() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (now - lastTelegramReport < config::TELEGRAM_REPORT_INTERVAL_MS) {
    return;
  }
  lastTelegramReport = now;

  if (!measurementAggregator.hasSamples()) {
    if (config::ENABLE_DATA_FETCH) {
      sendTelegramMessage(String(config::TELEGRAM_NO_DATA_MESSAGE));
    }
    return;
  }

  const MeasurementStats stats = measurementAggregator.stats();
  const String message = formatMeasurementReport(stats);
  if (sendTelegramMessage(message)) {
    measurementAggregator.reset();
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

void setup() {
  Serial.begin(115200);
  blinkController.begin(LED_PIN, LED_ACTIVE_LEVEL, LED_INACTIVE_LEVEL);
  setLedMode(LedMode::Normal);

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

  if (currentMode != LedMode::DataError && currentMode != LedMode::Normal) {
    setLedMode(LedMode::Normal);
  }

  delay(10);
}
