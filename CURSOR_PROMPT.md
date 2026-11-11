# Промпт для Cursor — проект Валера

Скопируйте содержимое ниже в первое сообщение чата Cursor после открытия репозитория.

---

Ты работаешь в репозитории XiaoZhi ESP32 v2.5.2 (база 49b4380), уже с правками под проект «Валера». Задача — одна прошивка для платы Waveshare ESP32-S3-Touch-AMOLED-1.75 (обычная 1.75, НЕ 1.75C): 16 МБ Flash, 8 МБ PSRAM, ESP32-S3R8, два микрофона ES7210, кодек ES8311, AMOLED CO5300 466×466, MCLK GPIO42.

## Архитектура
- XiaoZhi — единственная основа. Не создавать второй BIN, не переписывать аудиотракт, микрофон, AEC, сервер, TTS, Wi-Fi NVS.
- Циферблат, глаза, BME280 и погода встраиваются в то же приложение ESP-IDF как отдельная задача FreeRTOS, не блокирующая аудио.
- Запуск ассистента — вызов Application::WakeWordInvoke(...). Какая строка уйдёт на сервер, не важно. Важно, чтобы произнесённое «Валера» запустило ассистента.

## Аппаратная конфигурация (только 1.75)
- I2S: MCLK 42, BCLK 9, LRCK 45, DIN 10, DOUT 8, PA 46. Кодек I2C: SDA 15, SCL 14.
- BME280: SDA GPIO16, SCL GPIO17, 3.3 В, адреса 0x76/0x77. Отдельная шина. Без датчика — не зависать. На 1.75C GPIO16 — это MCLK, не использовать.
- Дисплей: CO5300, QSPI, CS 12, CLK 38, D0–D3 4–7, RST 39, сдвиг X 6. Тач CST9217: RST 40, INT 11.
- BOOT GPIO0: короткое — разговор, ~3 с — сброс Wi-Fi.

## Голосовой помощник
- Имя: Валера. Слово пробуждения: «Валера».
- Текущий config.json платы 1.75: MultiNet7 EN, команда `va le ra`, отображение/сервер `Валера`, порог 20, язык en. Это акустическое приближение, НЕ доказанное русское распознавание. Не называть это русской моделью.
- esp-sr поднят до 2.6.1. Штатный русский WakeNet10 `wn10_ru_privetesp` слышит только «Привет ESP». Не переименовывать его в «Валера».
- Каталог wake_models/ подготовлен под будущий wn10_ru_valera от Espressif. Когда придёт — выключить custom wake, включить USE_AFE_WAKE_WORD + SR_WN_WN10_RU_VALERA, пересобрать.
- Альтернативы, которые можно предложить, но не внедрять без выбора: microWakeWord на русских TTS «Валера»; посредник Vosk на ПК/телефоне, шлющий одну команду старта; облачное ухо Яндекс SpeechKit ru-RU с постоянной отдачей микрофона и WakeWordInvoke при «Валера» в тексте. DeepSeek — только мозг после старта, не ухо.

## Интерфейс (задача LVGL, отдельная от аудио)
- Состояния: IDLE, WAKEUP, LISTENING, THINKING, SPEAKING.
- Чёрный циферблат: большие часы, дата, день недели, анимированные глаза. Цвета — насыщенные циан/магента на чёрном AMOLED.
- Пилюля: улица / комната. Улица — Open-Meteo, Барнаул 53.347/83.777, Asia/Barnaul, без ключа. Комната — BME280. Если BME нет — лицо всё равно работает.
- Погода и BME в низкоприоритетной задаче, не блокируют аудио.
- Часы: сервер Xiaozhi уже отдаёт время со смещением timezone_offset. Не ставить TZ=UTC-7 — Барнаул удвоится. Пользователь ставит Asia/Barnaul в консоли Xiaozhi.

## Память и прошивка
- Таблица partitions/v2/16m.csv: nvs 0x9000, app с 0x20000, assets SPIFFS 8 МБ с 0x800000. Модели ESP-SR живут в assets. NVS не стирать.
- Прошивать без erase_flash: bootloader 0x0, partition-table 0x8000, app 0x20000, assets 0x800000.
- LOCK_LOCAL_WAKE_ASSETS=y: облако не должно подменять локальный набор моделей (issue #1190).

## Уже сделано в дереве
- main/idf_component.yml: espressif/esp-sr ==2.6.1
- main/boards/.../config.json: блок 1.75 как выше; блок 1.75C не трогать
- main/Kconfig.projbuild: CUSTOM_WAKE_WORD_LANG, AFE_WAKENET_THRESHOLD_X1000, LOCK_LOCAL_WAKE_ASSETS, SR_WN_WN10_RU_VALERA
- main/application.cc: при LOCK_LOCAL_WAKE_ASSETS игнорировать cloud download assets
- main/audio/engines/afe_audio_engine.cc: DET_MODE_95, порог, лог (неактивно пока USE_AFE_WAKE_WORD=n)
- main/audio/wake_words/custom_wake_word.cc: язык из Kconfig, лог prob_milli
- scripts/build_default_assets.py: поиск wake_models/, ключ multinet_model
- main/boards/.../valera_sensors.h/.cc: BME280 + Open-Meteo, НЕ вызваны из конструктора платы
- main/boards/.../valera_face.h: объявление, valera_face.cc ещё нет
- Плата .cc (esp32-s3-touch-amoled-1.75.cc) НЕ пропатчена: не перезаписывать целиком

## Незавершено
1. Написать valera_face.cc (подкласс CustomLcdDisplay).
2. Точечно пропатчить board .cc: ValeraSensors::Start(), длинное нажатие BOOT 3000 мс → EnterWifiConfigMode, климат MCP. Не затирать файл целиком.
3. Собрать одну прошивку. ESP-IDF в этой среде не установлен — не утверждать, что сборка прошла, пока её нет.
4. Не выдавать публичные merged-binary.bin / valera-face.bin за Валеру.

## Ограничения
- Не ломать драйверы аудио, дисплея, сети.
- Не утверждать, что «Валера» распознаётся, без физической проверки на плате.
- Не считать "va le ra" русским распознаванием.
- Не переименовывать wn10_ru_privetesp.
- Не делать два переключаемых BIN.
- Не вызывать erase_flash.

Сначала покажи план изменений по файлам, потом код. Не заявляй о сборке, пока её нет.
