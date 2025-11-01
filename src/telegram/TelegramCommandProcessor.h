#pragma once

#include <Arduino.h>

#include "protection/ProtectionController.h"
#include "protection/ProtectionStorage.h"
#include "telegram/TelegramService.h"

namespace telegram {

class TelegramCommandProcessor {
public:
  TelegramCommandProcessor(protection::ProtectionController &protection,
                           protection::ProtectionSettingsStorage &storage,
                           TelegramService &service);

  void processCommand(const String &text, const String &chatId, unsigned long now,
                      const sensor::MeasurementStats &objectStats);

private:
  bool isValidNumber(const String &value, bool allowDecimal) const;

  protection::ProtectionController &protection_;
  protection::ProtectionSettingsStorage &storage_;
  TelegramService &service_;
};

}  // namespace telegram

