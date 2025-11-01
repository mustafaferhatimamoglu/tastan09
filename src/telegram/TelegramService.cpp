#include "telegram/TelegramService.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "telegram/TelegramCommandProcessor.h"

namespace telegram {
namespace {
constexpr unsigned long TELEGRAM_POLL_INTERVAL_MS = 2000;
constexpr size_t TELEGRAM_MAX_JSON_SIZE = 4096;
}

TelegramService::TelegramService()
    : alertChatId_(String(config::TELEGRAM_ALERT_CHAT_ID)),
      infoChatId_(String(config::TELEGRAM_INFO_CHAT_ID)),
      secondaryChatId_(String(config::TELEGRAM_SECONDARY_CHAT_ID)) {}

bool TelegramService::configured() const {
  return config::ENABLE_TELEGRAM && strlen(config::TELEGRAM_BOT_TOKEN) > 0;
}

bool TelegramService::sendAlert(const String &text) {
  bool sent = false;
  if (alertChatId_.length() > 0) {
    sent |= sendMessageInternal(text, alertChatId_);
  } else if (infoChatId_.length() > 0) {
    sent |= sendMessageInternal(text, infoChatId_);
  }
  sent |= sendToSecondary(text, alertChatId_, infoChatId_);
  return sent;
}

bool TelegramService::sendInfo(const String &text) {
  if (infoChatId_.length() > 0) {
    bool sent = sendMessageInternal(text, infoChatId_);
    sent |= sendToSecondary(text, infoChatId_, alertChatId_);
    return sent;
  }
  return sendAlert(text);
}

bool TelegramService::sendDirect(const String &text, const String &chatId) {
  return sendMessageInternal(text, chatId);
}

void TelegramService::trySendStartupMessage() {
  if (startupMessageSent_ || !configured() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  bool sentAny = false;
  if (strlen(config::TELEGRAM_START_MESSAGE) > 0) {
    sentAny |= broadcast(String(config::TELEGRAM_START_MESSAGE));
  }
  if (strlen(config::TELEGRAM_USAGE_MESSAGE) > 0) {
    sentAny |= broadcast(String(config::TELEGRAM_USAGE_MESSAGE));
  }
  if (sentAny) {
    startupMessageSent_ = true;
  }
}

void TelegramService::pollUpdates(unsigned long now, TelegramCommandProcessor &processor,
                                  const sensor::MeasurementStats &objectStats) {
  if (!configured() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (now - lastPoll_ < TELEGRAM_POLL_INTERVAL_MS) {
    return;
  }
  lastPoll_ = now;

  WiFiClientSecure client;
  if (config::TELEGRAM_ALLOW_INSECURE_TLS) {
    client.setInsecure();
  }

  HTTPClient https;
  String url = String(F("https://api.telegram.org/bot")) + config::TELEGRAM_BOT_TOKEN + F("/getUpdates?timeout=0");
  if (lastUpdateId_ > 0) {
    url += F("&offset=");
    url += String(lastUpdateId_ + 1);
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

  if (payload.length() > TELEGRAM_MAX_JSON_SIZE) {
    Serial.println(F("Telegram: yanit verisi cok buyuk"));
    return;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print(F("Telegram JSON hatasi: "));
    Serial.println(error.c_str());
    return;
  }

  if (!doc["result"].is<JsonArray>()) {
    return;
  }

  for (JsonObject update : doc["result"].as<JsonArray>()) {
    const long updateId = update["update_id"] | 0;
    if (updateId <= lastUpdateId_) {
      continue;
    }
    lastUpdateId_ = updateId;

    JsonObject messageObj = update["message"].as<JsonObject>();
    if (messageObj.isNull()) {
      continue;
    }

    const String chatId = messageObj["chat"]["id"].as<String>();
    if (!isAuthorizedChat(chatId)) {
      Serial.print(F("Telegram: yetkisiz chat: "));
      Serial.println(chatId);
      continue;
    }

    String text = messageObj["text"] | "";
    text.trim();
    if (text.length() == 0) {
      continue;
    }

    processor.processCommand(text, chatId, now, objectStats);
  }
}

bool TelegramService::isAuthorizedChat(const String &chatId) const {
  if (chatId.length() == 0) {
    return false;
  }
  if (alertChatId_.length() > 0 && chatId == alertChatId_) {
    return true;
  }
  if (infoChatId_.length() > 0 && chatId == infoChatId_) {
    return true;
  }
  if (secondaryChatId_.length() > 0 && chatId == secondaryChatId_) {
    return true;
  }
  if (alertChatId_.length() == 0 && infoChatId_.length() == 0 && secondaryChatId_.length() == 0) {
    return true;
  }
  return false;
}

bool TelegramService::sendToSecondary(const String &text, const String &avoid1, const String &avoid2) {
  if (secondaryChatId_.length() == 0) {
    return false;
  }
  if ((avoid1.length() > 0 && secondaryChatId_ == avoid1) || (avoid2.length() > 0 && secondaryChatId_ == avoid2)) {
    return false;
  }
  return sendMessageInternal(text, secondaryChatId_);
}

bool TelegramService::broadcast(const String &text) {
  bool sent = false;
  if (alertChatId_.length() > 0) {
    sent |= sendMessageInternal(text, alertChatId_);
  }
  if (infoChatId_.length() > 0 && infoChatId_ != alertChatId_) {
    sent |= sendMessageInternal(text, infoChatId_);
  }
  if (secondaryChatId_.length() > 0 && secondaryChatId_ != alertChatId_ && secondaryChatId_ != infoChatId_) {
    sent |= sendMessageInternal(text, secondaryChatId_);
  }
  return sent;
}

bool TelegramService::sendMessageInternal(const String &text, const String &chatId) {
  if (!configured()) {
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

String TelegramService::urlEncode(const String &value) {
  String encoded;
  encoded.reserve(value.length() * 3);
  const char hex[] = "0123456789ABCDEF";

  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
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

}  // namespace telegram
