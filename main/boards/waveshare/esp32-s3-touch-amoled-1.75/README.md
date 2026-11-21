# Waveshare ESP32-S3-Touch-AMOLED-1.75 — Валера

Одна прошивка только под плату **1.75** (не 1.75C у автора видео):
MCLK GPIO42, BME280 на GPIO16/17. Вариант 1.75c из `config.json` убран.

## Первый запуск

Пошагово: Wi‑Fi, регистрация на **xiaozhi.me**, привязка, жесты, wake —  
см. **[SETUP.md](./SETUP.md)**. Идеи развития: **[IDEAS.md](./IDEAS.md)**. ТЗ: **[TZ.md](./TZ.md)**.

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

## Wi‑Fi (первый boot)

Как в остальных XiaoZhi `WifiBoard`:

1. Если в NVS уже есть SSID — станция подключается сама.
2. Если SSID нет (или таймаут / длинное BOOT) — hotspot provisioning:
   точка `Xiaozhi-XXXX` → браузер (обычно `http://192.168.4.1`) → выбор сети.
3. На плате Valera включено `CONFIG_USE_HOTSPOT_WIFI_PROVISIONING=y`
   (BluFi выключен). Экран лица в `WifiConfiguring` / `Starting` / `Connecting`
   показывает SSID точки и URL — не прячется в status bar.
4. MCP `self.system.reconfigure_wifi` и длинное BOOT снова входят в настройку.
   Cold boot **не** стирает сохранённый Wi‑Fi.

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

## Циферблат и лицо (Popka + Sber)

Визуал и структура окон — как у [PopkaStudio](https://github.com/PopaSpinka/PopkaStudio)
(автор разрешил использовать код; проект некоммерческий).

**Idle:** Modern-часы ↔ погода ↔ playful-глаза (Sber) ↔ процедурное слайд-шоу.
Свайп ←/→ листает страницы; свайп ↓ с верха открывает **шторку** настроек
(яркость, громкость, тема, сброс Wi‑Fi).

**Темы / стиль часов** (NVS `valera` + MCP `self.valera.clock_style`):

| look | вид |
| --- | --- |
| 0 Modern | белые HH / coral MM (или свои `hh_color`/`mm_color`) |
| 1 Neon | cyan / magenta |
| 2 Soft | светло-розовые / coral |
| 3 Retro | янтарный / терракота |
| 4 Digital | mint / teal |
| `scale_pct` | 80…160 (масштаб через LVGL zoom) |

**Слушает ≠ отвечает:**

| стадия | UI |
| --- | --- |
| Listen | глаза + **cyan** рот-EQ, 11 симметричных столбиков (L↔R mirror) |
| Think | глаза + violet/magenta EQ, медленнее |
| Speak | текст без рамки + **coral/magenta** voice-EQ (та же геометрия 11 баров) |

Эмоции (`SetEmotion`) только двигают взгляд/прищур — **не** ломают стадии устройства.

**Sber-подобные эффекты (да, реализованы):** idle-страница `kPlay` —
двойные дыхающие кольца (`play_ring_` / `play_ring2_`), орбитальные орбы,
мягкая «смывка» цвета глаз cyan↔magenta и лёгкий прищур. Это screensaver в
цикле простоя, не отдельный платёжный UI.

Предпросмотр: [`ui-preview.html`](./ui-preview.html), скрины `ui-screens/`.

```
Циферблат ──► Погода / Глаза(Sber) / Слайды / Shade↓ / Радио / Галерея
Wake «Валера» ──► Слушает(cyan EQ) ──► Думает ──► Ответ(coral EQ) ──► idle
Радио ──► Станции (папки, скролл Popka, скрытие) / Громкость
Wi‑Fi ──► hotspot Xiaozhi-XXXX → браузер
```

Карта жестов: `ui-preview.html` → «Карта жестов», скрин `ui-screens/14-map.webp`.
Сводное ТЗ: [`TZ.md`](./TZ.md).

### Российское радио (Popka poster)

Превью `ui-preview.html` / скрины `09-radio`, `10–11-stations`:

- **Лучи:** 23×2 симметричных capsule-луча (низ +32° → верх −64°, L = зеркало R)
- **Трек:** ICY-стиль marquee на постере; в списке у играющей станции — `♪ название`
- **Edge Play/Stop** по дуге круга, как у автора
- **Скрытие:** NVS `radio_hide` + MCP `radio_hide` / `radio_restore` / `radio_hidden`

Пресеты (HTTP 200 + `audio/*`): `valera_radio_stations.h` — 23 РФ-станции.
MCP: `self.valera.radio_stations` (только видимые). Декод потока + живой ICY в LVGL — на железе.

### Память Flash (16 МБ)

CI `waveshare-esp32-s3-touch-amoled-1.75`:

| образ | размер | лимит раздела | запас |
| --- | --- | --- | --- |
| `xiaozhi.bin` (app) | ≈ 3.05 МБ | OTA 3.94 МБ | ~23% |
| `generated_assets.bin` | ≈ 2.57 МБ | assets 8 МБ | много |
| bootloader | ≈ 15 КБ | ок | ~53% |

**Влезает.** Пресеты радиостанций — килобайты строк.

### TF / microSD

На плате слот есть (SDMMC 1-bit: CMD=GPIO1, CLK=GPIO2, D0=GPIO3).
В текущей Valera/XiaoZhi прошивке карта ещё не монтируется — можно добавить
для музыки/кэша; пины не пересекаются с дисплеем и аудио (MCLK=GPIO42).

AMOLED CO5300 QSPI 466×466, штатные драйверы XiaoZhi/Waveshare:

- LVGL `timer_period_ms = 16`, лицо тикает каждые 16 ms
- partial refresh, double buffer в PSRAM, DMA bounce
- QSPI `pclk` 80 MHz
- обновляются только грязные виджеты (глаза / двоеточие / пилюли)

Аудио остаётся на своём ядре/приоритете и не блокируется отрисовкой.

Часовой пояс — `Asia/Barnaul` в консоли Xiaozhi; `TZ=UTC-7` не ставить.

## Документация платы

[ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm)
