#pragma once

#include <EEPROM.h>

#include "protection/ProtectionSettings.h"

namespace protection {

class ProtectionSettingsStorage {
public:
  bool load(ProtectionSettings &settings);
  bool save(const ProtectionSettings &settings);

private:
  bool init();

  bool initialized_{false};
};

}  // namespace protection

