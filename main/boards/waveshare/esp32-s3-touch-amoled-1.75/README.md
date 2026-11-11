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

## Русское пробуждение «Валера» (бесплатно)

Локальный детектор `USE_VALERA_RU_LOCAL_EAR`: AFE mono → MFCC → DTW по вшитым русским TTS-шаблонам «Валера» → `WakeWordInvoke`.

- Без платного облачного STT и без постоянной отдачи микрофона в облако.
- Не English MultiNet `va le ra`.
- Не переименованный `wn10_ru_privetesp`.
- BOOT (короткое) — аварийный старт разговора; длинное (~3 с) — сброс Wi-Fi.

Порог DTW и шаблоны могут потребовать подстройки на живом голосе; без проверки на плате успех не утверждается.

Когда Espressif пришлёт `wn10_ru_valera`, можно перейти на WakeNet (см. `wake_models/README.txt`).

## Циферблат 60 fps

AMOLED CO5300 QSPI 466×466, штатные драйверы XiaoZhi/Waveshare:

- LVGL `timer_period_ms = 16`, лицо тикает каждые 16 ms
- partial refresh, double buffer в PSRAM, DMA bounce
- QSPI `pclk` 80 MHz
- обновляются только грязные виджеты (глаза / двоеточие / пилюли)

Аудио остаётся на своём ядре/приоритете и не блокируется отрисовкой.

Часовой пояс — `Asia/Barnaul` в консоли Xiaozhi; `TZ=UTC-7` не ставить.

## Документация платы

[ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm)
