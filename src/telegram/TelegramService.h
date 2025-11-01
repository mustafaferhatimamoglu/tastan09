#pragma once

#include <Arduino.h>

#include "sensor/MeasurementAggregator.h"

namespace telegram {

class TelegramCommandProcessor;

class TelegramService {
public:
  TelegramService();

  bool configured() const;
  bool sendAlert(const String &text);
  bool sendInfo(const String &text);
  bool sendDirect(const String &text, const String &chatId);
  void trySendStartupMessage();
  void pollUpdates(unsigned long now, TelegramCommandProcessor &processor,
                   const sensor::MeasurementStats &objectStats);

  void resetStartupFlag() { startupMessageSent_ = false; }

private:
  bool isAuthorizedChat(const String &chatId) const;
  bool sendMessageInternal(const String &text, const String &chatId);
  static String urlEncode(const String &value);

  bool startupMessageSent_{false};
  long lastUpdateId_{0};
  unsigned long lastPoll_{0};
  String alertChatId_;
  String infoChatId_;
};

}  // namespace telegram

