# Waveshare ESP32-S3-Touch-AMOLED-1.75 — Валера

Обычная плата **1.75** (не 1.75C): MCLK GPIO42, BME280 на GPIO16/17.

## Прошивка

```sh
source /path/to/esp-idf/export.sh
python3 scripts/build.py waveshare/esp32-s3-touch-amoled-1.75 \
  --name esp32-s3-touch-amoled-1.75 \
  --language ru-RU
```

Не вызывать `erase_flash`. Прошивка:

| образ | адрес |
| --- | --- |
| bootloader | 0x0 |
| partition-table | 0x8000 |
| app | 0x20000 |
| assets | 0x800000 |

## Русское пробуждение «Валера»

Выбрано **облачное ухо**: AFE отдаёт очищенный mono PCM → energy gate → Yandex SpeechKit STT `ru-RU` → при тексте со словом «Валера» вызывается обычный `WakeWordInvoke`.

Это не English MultiNet `va le ra` и не переименованный `wn10_ru_privetesp`.

Нужны ключи в NVS (пространство `valera`):

- `api_key` — Yandex Cloud Api-Key
- `folder_id` — ID каталога

Задать через MCP-инструмент `self.valera.set_yandex_stt` после подключения к xiaozhi.me. Без ключей голосовое пробуждение молчит; **BOOT** (короткое нажатие) остаётся запасным стартом разговора. Длинное нажатие BOOT (~3 с) — сброс Wi-Fi.

Когда Espressif пришлёт настоящий `wn10_ru_valera`, можно переключить на `USE_AFE_WAKE_WORD` + `SR_WN_WN10_RU_VALERA` (см. `wake_models/README.txt`).

## Циферблат

AMOLED 466×466: большие часы, дата, глаза, пилюли «улица» (Open-Meteo, Барнаул) и «комната» (BME280). Часовой пояс — `Asia/Barnaul` в консоли Xiaozhi; `TZ=UTC-7` не ставить.

## Документация платы

[ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm)
