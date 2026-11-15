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
### Кнопки на плате

| Кнопка | Действие |
| --- | --- |
| **BOOT** короткое | старт/стоп разговора (аварийно, если wake не сработал) |
| **BOOT** длинное ~3 с | сброс Wi-Fi / режим настройки |
| **BOOT** двойное | вкл/выкл Device AEC (в idle) |
| **PWR** короткое | разбудить + громкость +10 (после 100 → 20) |
| **PWR** длинное ~1.8 с | мягкое PowerOff через AXP2101 |
| **PWR** аппаратно ~4 с | hard off AXP (штатная защита PMIC) |

PWR читается с TCA9554 EXIO4 (`SYS_OUT`), BOOT — GPIO0.

### Перезагрузка отключением питания

Питание выдернули → SRAM/PSRAM очищаются сами. NVS (Wi‑Fi) **не** стираем при каждом cold boot — иначе каждый unplug сбрасывает сеть. При `ESP_RST_POWERON` / brownout UI-сессия сбрасывается (чат, sleep). Повреждённый NVS уже чинит `main.cc` (`nvs_flash_erase` только при corruption). Для «завис» удобнее короткое soft-off по PWR long, чем рвать питание mid-write.

Порог DTW и шаблоны могут потребовать подстройки на живом голосе; без проверки на плате успех не утверждается.

Когда Espressif пришлёт `wn10_ru_valera`, можно перейти на WakeNet (см. `wake_models/README.txt`).

## Циферблат 60 fps

Визуал и структура окон — как у [PopkaStudio](https://github.com/PopaSpinka/PopkaStudio)
(автор разрешил использовать код; проект некоммерческий).

**Сейчас:** Stack-часы (сплошные HH белые / MM coral), погода, игривые глаза,
голос (слушает → думает → ответ без рамки).

**План (как карта Popka):** радио Poster + Browse + громкость, shade быстрых
настроек, галерея приложений. Интерактивный предпросмотр:
[`ui-preview.html`](./ui-preview.html) (скриншоты `ui-preview-*.webp`).

```
Циферблат ──► Погода / Глаза idle / Shade↓ / Радио / Галерея
Wake «Валера» ──► Слушает ──► Думает ──► Ответ ──► idle
Радио ──► Станции (папки) / Громкость
```

AMOLED CO5300 QSPI 466×466, штатные драйверы XiaoZhi/Waveshare:

- LVGL `timer_period_ms = 16`, лицо тикает каждые 16 ms
- partial refresh, double buffer в PSRAM, DMA bounce
- QSPI `pclk` 80 MHz
- обновляются только грязные виджеты (глаза / двоеточие / пилюли)

Аудио остаётся на своём ядре/приоритете и не блокируется отрисовкой.

Часовой пояс — `Asia/Barnaul` в консоли Xiaozhi; `TZ=UTC-7` не ставить.

## Документация платы

[ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm)
