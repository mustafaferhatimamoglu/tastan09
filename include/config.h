#pragma once

namespace config {
constexpr char WIFI_SSID[] = "TurkTelekom_TPBA52_2.4GHz";
constexpr char WIFI_PASSWORD[] = "hbJ39MkCMJa9";
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 15000;

constexpr uint8_t I2C_SDA_PIN = D2; // NodeMCU GPIO4
constexpr uint8_t I2C_SCL_PIN = D1; // NodeMCU GPIO5

constexpr bool ENABLE_DATA_FETCH = true;       // Enable MLX90614 measurements
constexpr unsigned long MEASUREMENT_INTERVAL_MS = 1000; // Sample every second

constexpr bool ENABLE_TELEGRAM = true;
constexpr char TELEGRAM_BOT_TOKEN[] = "8323126146:AAGcQUHIvtDSvo4Y3o9ASztQAMT18pQLHWQ";
constexpr char TELEGRAM_CHAT_ID[] = "6069420562";
constexpr unsigned long TELEGRAM_REPORT_INTERVAL_MS = 10000;
constexpr bool TELEGRAM_ALLOW_INSECURE_TLS = true;
constexpr char TELEGRAM_START_MESSAGE[] = "Cihaz baslatildi.";
constexpr char TELEGRAM_NO_DATA_MESSAGE[] = "Son periyotta olcum verisi bulunamadi.";

constexpr bool ENABLE_PROTECTION = true;
constexpr float OBJECT_TEMP_MIN_C = 20.0f;
constexpr float OBJECT_TEMP_MAX_C = 30.0f;
constexpr float OBJECT_TEMP_HYSTERESIS_C = 1.0f;
constexpr size_t PROTECTION_MIN_SAMPLES = 5;
constexpr unsigned long PROTECTION_RENOTIFY_INTERVAL_MS = 120000; // re-notify interval
constexpr uint8_t HEATING_RELAY_PIN = D5;
constexpr uint8_t COOLING_RELAY_PIN = D6;
constexpr uint8_t HEATING_RELAY_ACTIVE_LEVEL = LOW;
constexpr uint8_t COOLING_RELAY_ACTIVE_LEVEL = LOW;
constexpr unsigned long RELAY_MIN_SWITCH_INTERVAL_MS = 5000;
}
