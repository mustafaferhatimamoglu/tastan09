# Tastan09 ESP8266 Projesi

Bu proje NodeMCU (ESP8266) tabanli bir temassiz sicaklik izleme ve koruma sistemini PlatformIO ve Arduino framework kullanarak gerceklestirir. MLX90614 sicaklik sensoru, role ile isitma/sogutma kontrolu ve Telegram uzerinden bildirim/komut destegi saglar.

## Ozellikler
- MLX90614 ile nesne ve ortam sicakligi olcumu, kayan pencere istatistikleri
- Sicaklik sinirlari, histerezis ve son olcum verisine gore hizli tepki veren koruma kontrolu
- Duruma gore yonetilen LED durum gosterge desenleri
- Telegram uzerinden bildirim, durum raporu ve konfigurasyon komutlari
- Ek Telegram kanali ile cift tarafli komut/bildirim destegi
- EEPROM uzerinde koruma ayarlarini kalici saklama

## Donanim Gereksinimleri
- NodeMCU 0.9 (ESP-12) veya uyumlu ESP8266 karti
- MLX90614 IR sicaklik sensoru (I2C baglanti)
- Isitma ve sogutma icin 2x role modulu
- Status LED (board uzerindeki LED_BUILTIN kullaniliyor)

Pin dagilimi `docs/pinout.txt` dosyasinda detaylandirilmis.

## Kurulum
1. PlatformIO kurulumu ve gerekli kart kutuphanelerini yukleyin.
2. `include/config.h` icindeki Wi-Fi, Telegram (`TELEGRAM_ALERT_CHAT_ID`, `TELEGRAM_INFO_CHAT_ID`, `TELEGRAM_SECONDARY_CHAT_ID`, `TELEGRAM_USAGE_MESSAGE`) ve role/sicaklik ayarlarini ihtiyaciniza gore duzenleyin.
3. Cihazinizi USB uzerinden baglayin ve `platformio.ini` dosyasindaki `upload_port` degerinin dogru oldugundan emin olun.
4. Derleme icin:
   ```
   platformio run
   ```
5. Yukleme icin:
   ```
   platformio run --target upload --upload-port COM4
   ```
6. Seri port izleme icin:
   ```
   platformio device monitor --baud 115200
   ```

## Yapilanlar
- LED durum desenleri `blink/BlinkController` sinifi ile kapsullendi ve yeniden kullanilabilir hale getirildi.
- Koruma islemleri `protection` modulu altindaki siniflarla (ayar, kontrol, saklama) katmanlandi.
- Son olcum tabanli koruma tetikleyici ve histerezis ile role cikislari daha stabil hale getirildi.
- Telegram servis ve komut isleme `telegram` modulu ile ayristirildi, tum bildirimler tanimli tum chat ID'lerine dagitiliyor.
- Baslangicta otomatik kullanici rehberi iletiliyor ve ek chat kimligi (secondary) komut yetkisi aliyor.
- EEPROM saklama formati imzali, versiyonlu ve checksum kontrollu olarak tasarlandi.

## Eksikler ve Iyilestirme Firsatlari
- Otomatik birim testleri bulunmuyor; ozellikle koruma mantigi ve komut parsleme icin birim testleri eklenecek.
- Konfigurasyon degerleri (Wi-Fi, bot tokeni) kaynak kodda tutuluyor; guvenlik icin harici bir gizli ayar mekanizmasi tasarlanabilir.
- Role cikislari icin donanimsal ariza tespiti ve watchdog mekanizmasi eklenebilir.
- Telegram API cevap boyutu artisinda daha akilli parcalama/filtreleme yapilabilir.
- MLX90614 hata durumlarinda tekrar deneme ve hata kodlarini raporlama gelistirilebilir.

## Telegram Kullanim
- Bot tokenini ve chat ID'lerini `include/config.h` uzerinden doldurun. `TELEGRAM_SECONDARY_CHAT_ID` opsiyonel 
  fakat tanimlanirsa tum bildirimler oraya da iletilir ve komut kabul edilir.
- Cihaz Wi-Fi baglantisindan sonra `TELEGRAM_START_MESSAGE` ve `TELEGRAM_USAGE_MESSAGE` degerlerini tum yetkili
  chat'lere otomatik olarak gonderir. Mesajlari ihtiyaca gore ozellestirebilirsiniz.
- Desteklenen komutlar: `config`, `set min <deger_C>`, `set max <deger_C>`, `set hysteresis <deger_C>`,
  `set minsamples <tam_sayi>`, `set renotify <saniye>`. Gecerli komutlar EEPROM'a kaydedilir ve koruma mantigi
  aninda yeniden degerlendirilir.
- Telegram uzerinden komut gonderirken mesaj basinda/sonunda bosluk birakmamaya dikkat edin; yetkisiz chat ID'leri
  seri porta uyari olarak yazilir.

## Testler ve Kontrol Listesi
- [x] `platformio run` derlemesi basarili (ESP8266 hedefi icin)
- [ ] Fiziksel cihaz uzerinde isitma/sogutma role gecisleri dogrulandi
- [ ] MLX90614 sensor okumalari ve hata senaryolari test edildi
- [ ] Telegram botu uzerinden `config` ve `set` komutlari pilot ortamda denendi
- [ ] Wi-Fi kesinti senaryosunda yeniden baglanti akisi dogrulandi

Testleri calistirmak icin temel komutlar:
```bash
platformio run                         # Derleme
platformio device monitor --baud 115200 # Seri log
```
Elektriksel cikislari test ederken rolelerin dogru acik/kapali seviyelerinde calistigini multimetre veya LED ile dogrulayin.

## Proje Yapisi
- `src/main.cpp`: Uygulama girisi, modul baglantilari
- `src/blink`: LED gosterge mantigi
- `src/protection`: Koruma ayarlari, kontrol ve EEPROM saklama
- `src/sensor`: Sensor soyutlamalari ve istatistik hesaplama
- `src/telegram`: Telegram servis baglantisi ve komut isleme
- `include/config.h`: Donanim ve servis konfigurasyon sabitleri
- `docs/pinout.txt`: Donanim baglanti referansi

## Bilinen Limitler
- `config.h` icinde saklanan sifre/bot tokenleri binary icine gomuluyor.
- Telegram JSON parse kapasitesi 4 KB ile sinirli; daha buyuk cevaplar atlanir.
- Otomatik OTA guncelleme veya kablosuz yazilim guncellemesi bulunmuyor.

## Katki
Gelisim icin yeni gorevleri `Eksikler ve Iyilestirme Firsatlari` bolumune ekleyebilir, her duzenleme sonrasi `platformio run` ile derleme kontrolu yapabilirsiniz.
