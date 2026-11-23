# Прошивка Valera на Windows (COM4)

Плата: **ESP32-S3-Touch-AMOLED-1.75**  
Готовый файл из CI: **`merged-binary.bin`** → пишется с адреса **`0x0`**.

Я (облачный агент) **не вижу ваш COM4** — прошивку запускаете вы на своём ПК.

---

## 0. Подготовка (один раз)

1. USB кабель **data** (не «только зарядка»).
2. В Диспетчере устройств должен быть порт **COM4** (или другой COMx).
3. Если порта нет / «неизвестное устройство» — драйвер:
   - часто **CP210x** или **CH340** (смотрите чип у USB на плате Waveshare).
4. Python 3 с pip (для способа А) **или** Flash Download Tool (способ Б).

---

## 1. Скачать прошивку

### Вариант из GitHub Actions (наш PR)

1. Откройте Actions ветки:  
   https://github.com/iona1234/valera-ai/actions?query=branch%3Acursor%2Fvalera-amoled-1-75-cb17  
2. Возьмите **последний зелёный** run → Artifacts →  
   `xiaozhi_waveshare-esp32-s3-touch-amoled-1.75_…`  
3. Распакуйте ZIP → внутри файл **`merged-binary.bin`**.

Не делайте `erase_flash`, если не хотите стереть Wi‑Fi.  
Если Wi‑Fi «залип» в битых данных — один раз можно стереть NVS отдельно
(спросите агента) или полный erase только как крайняя мера.

---

## 2. Способ А — одна команда (рекомендую)

В PowerShell:

```powershell
pip install esptool
```

Положите `merged-binary.bin` например в `D:\valera\` и выполните:

```powershell
cd D:\valera
python -m esptool --chip esp32s3 -p COM4 -b 460800 write_flash 0x0 merged-binary.bin
```

Если не шьётся:

1. Зажмите кнопку **BOOT** на плате.
2. Воткните/перевоткните USB (или нажмите **RESET**, не отпуская BOOT).
3. Отпустите BOOT.
4. Снова запустите команду выше.

Успех выглядит примерно так: `Hash of data verified` / `Leaving... Hard resetting...`.

---

## 3. Способ Б — программа с окошками

1. Скачайте **Flash Download Tools** (Espressif).
2. ChipType: **ESP32-S3**, SPI SPEED 40MHz, SPI MODE DIO, Flash 16MBit×8 или по факту **16 MB**.
3. SPIDownload → галочка на файл → путь к `merged-binary.bin` → адрес **`0x0`**.
4. COM: **COM4**, Baud: **460800** (или 921600).
5. **START**. Если не идёт — BOOT+RESET как выше.

---

## 4. После прошивки

1. Отпустите BOOT, нажмите RESET (или переподключите USB).
2. На экране должен появиться циферблат Valera / Wi‑Fi-экран.
3. Дальше:
   - Wi‑Fi: точка `Xiaozhi-XXXX` → http://192.168.4.1 → домашняя сеть **2.4 ГГц**;
   - **куда идти дальше:** **[NEXT_STEPS.md](./NEXT_STEPS.md)** → сайт **https://xiaozhi.me** → код привязки;
   - полный гайд: **[SETUP.md](./SETUP.md)**.

---

## 5. Частые ошибки

| Симптом | Что делать |
| --- | --- |
| `Could not open COM4` | Закройте Arduino Serial Monitor / другой терминал; проверьте COM в Диспетчере |
| `Failed to connect` | BOOT+RESET; другой кабель; Baud 115200 |
| Чёрный экран после flash | Подождите 5–10 с; RESET; убедитесь что файл именно **1.75**, не 1.75c |
| Порт исчезает при flash | Драйвер; USB хаб убрать; воткнуть в задний порт ПК |

---

## Адреса (если шьёте по частям, не merged)

Обычно **не нужно** — merged уже всё содержит:

| образ | адрес |
| --- | --- |
| bootloader | `0x0` |
| partition-table | `0x8000` |
| app | `0x20000` |
| assets | `0x800000` |
