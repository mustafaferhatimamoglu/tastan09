#include "telegram/TelegramCommandProcessor.h"

#include "config.h"

namespace telegram {

TelegramCommandProcessor::TelegramCommandProcessor(protection::ProtectionController &protection,
                                                   protection::ProtectionSettingsStorage &storage,
                                                   TelegramService &service)
    : protection_(protection), storage_(storage), service_(service) {}

void TelegramCommandProcessor::processCommand(const String &text, const String &chatId, unsigned long now,
                                              const sensor::MeasurementStats &objectStats) {
  String trimmed = text;
  trimmed.trim();
  if (trimmed.length() == 0) {
    return;
  }

  String lower = trimmed;
  lower.toLowerCase();

  if (lower == F("config")) {
    service_.sendDirect(protection_.formatProtectionConfig(), chatId);
    return;
  }

  if (!lower.startsWith(F("set "))) {
    service_.sendDirect(F("Bilinmeyen komut. 'config' veya 'set ...' kullanin."), chatId);
    return;
  }

  const int keyStart = 4;
  const int spaceIndex = lower.indexOf(' ', keyStart);
  if (spaceIndex < 0) {
    service_.sendDirect(F("Eksik parametre. Ornek: set min 22.5"), chatId);
    return;
  }

  const String key = lower.substring(keyStart, spaceIndex);
  String valueText = trimmed.substring(spaceIndex + 1);
  valueText.trim();
  if (valueText.length() == 0) {
    service_.sendDirect(F("Deger bulunamadi. Ornek: set max 28.0"), chatId);
    return;
  }

  bool updated = false;
  String response;
  const protection::ProtectionSettings previousSettings = protection_.settings();

  if (key == F("min")) {
    if (!isValidNumber(valueText, true)) {
      service_.sendDirect(F("Gecersiz sayi. Ondalik icin nokta kullanin."), chatId);
      return;
    }
    const float newMin = valueText.toFloat();
    String error;
    if (!protection_.setMin(newMin, error)) {
      service_.sendDirect(error, chatId);
      return;
    }
    response = F("Ayar guncellendi: min = ");
    response += String(newMin, 2);
    response += F(" C");
    updated = true;
  } else if (key == F("max")) {
    if (!isValidNumber(valueText, true)) {
      service_.sendDirect(F("Gecersiz sayi. Ondalik icin nokta kullanin."), chatId);
      return;
    }
    const float newMax = valueText.toFloat();
    String error;
    if (!protection_.setMax(newMax, error)) {
      service_.sendDirect(error, chatId);
      return;
    }
    response = F("Ayar guncellendi: max = ");
    response += String(newMax, 2);
    response += F(" C");
    updated = true;
  } else if (key == F("hysteresis")) {
    if (!isValidNumber(valueText, true)) {
      service_.sendDirect(F("Gecersiz sayi. Ondalik icin nokta kullanin."), chatId);
      return;
    }
    const float newH = valueText.toFloat();
    String error;
    if (!protection_.setHysteresis(newH, error)) {
      service_.sendDirect(error, chatId);
      return;
    }
    response = F("Ayar guncellendi: histerezis = ");
    response += String(newH, 2);
    response += F(" C");
    updated = true;
  } else if (key == F("minsamples")) {
    if (!isValidNumber(valueText, false)) {
      service_.sendDirect(F("Gecersiz tam sayi."), chatId);
      return;
    }
    const long newSamples = valueText.toInt();
    if (newSamples < 0) {
      service_.sendDirect(F("Gecersiz tam sayi."), chatId);
      return;
    }
    String error;
    if (!protection_.setMinSamples(static_cast<size_t>(newSamples), error)) {
      service_.sendDirect(error, chatId);
      return;
    }
    response = F("Ayar guncellendi: min ornek sayisi = ");
    response += static_cast<unsigned long>(protection_.settings().minSamples);
    updated = true;
  } else if (key == F("renotify")) {
    if (!isValidNumber(valueText, false)) {
      service_.sendDirect(F("Gecersiz tam sayi."), chatId);
      return;
    }
    const long seconds = valueText.toInt();
    if (seconds < 0) {
      service_.sendDirect(F("Gecersiz tam sayi."), chatId);
      return;
    }
    String error;
    if (!protection_.setRenotifySeconds(static_cast<unsigned long>(seconds), error)) {
      service_.sendDirect(error, chatId);
      return;
    }
    response = F("Ayar guncellendi: renotify = ");
    response += seconds;
    response += F(" sn");
    updated = true;
  } else {
    service_.sendDirect(F("Bilinmeyen ayar anahtari. 'config' yazarak yardim alabilirsiniz."), chatId);
    return;
  }

  if (!updated) {
    return;
  }

  if (!protection::validateProtectionSettings(protection_.settings())) {
    protection_.applySettings(previousSettings);
    service_.sendDirect(F("Ayar guncellenemedi: yeni degerler uyumsuz."), chatId);
    return;
  }

  if (storage_.save(protection_.settings())) {
    response += F(" (kaydedildi)");
  } else {
    protection_.applySettings(previousSettings);
    response += F(" (EEPROM kaydedilemedi, eski ayarlar korunuyor)");
  }

  service_.sendDirect(response, chatId);
  service_.sendDirect(protection_.formatProtectionConfig(), chatId);
  protection_.handleProtection(objectStats, now);
}

bool TelegramCommandProcessor::isValidNumber(const String &value, bool allowDecimal) const {
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

}  // namespace telegram

