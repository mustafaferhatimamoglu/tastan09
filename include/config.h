#pragma once

namespace config {
constexpr char WIFI_SSID[] = "TurkTelekom_TPBA52_2.4GHz";     // Wi-Fi SSID (fill in)
constexpr char WIFI_PASSWORD[] = "hbJ39MkCMJa9"; // Wi-Fi password (fill in)
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 15000;

constexpr bool ENABLE_DATA_FETCH = false;       // Set true when readMeasurement() is implemented
constexpr unsigned long MEASUREMENT_INTERVAL_MS = 1000; // Sample every second

constexpr bool ENABLE_TELEGRAM = false;         // Enable after filling token and chat id
constexpr char TELEGRAM_BOT_TOKEN[] = "8323126146:AAGcQUHIvtDSvo4Y3o9ASztQAMT18pQLHWQ";      // Telegram bot token
constexpr char TELEGRAM_CHAT_ID[] = "6069420562";        // Destination chat id
constexpr unsigned long TELEGRAM_REPORT_INTERVAL_MS = 10000; // 10 seconds
constexpr bool TELEGRAM_ALLOW_INSECURE_TLS = true;            // Disable for production if you add certificates
constexpr char TELEGRAM_START_MESSAGE[] = "Cihaz baslatildi.";
constexpr char TELEGRAM_NO_DATA_MESSAGE[] = "Son periyotta olcum verisi bulunamadi.";
}
