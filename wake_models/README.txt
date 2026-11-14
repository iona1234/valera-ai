Сюда кладётся только настоящая модель WakeNet10 от Espressif.
Папка wn10_ru_privetesp сюда не копируется и не переименовывается: она
распознаёт «Привет ESP», не «Валера».

Текущая прошивка Валеры (плата 1.75) использует бесплатное локальное ухо
CONFIG_USE_VALERA_RU_LOCAL_EAR (MFCC+DTW по русским шаблонам «Валера»),
а не эту папку и не платный облачный STT.

Когда sales@espressif.com пришлёт модель на слово «Валера»:
1. Распаковать каталог с именем wn10_ru_valera.
2. Внутри для ESP32-S3 обязательны файлы wn10_data_p1 и _MODEL_INFO_p1
   (срез p2 — для P4/S31, сборщик XiaoZhi сам переименует p1 в wn10_data).
3. Положить каталог сюда: wake_models/wn10_ru_valera/.
4. В sdkconfig платы: выключить USE_VALERA_RU_LOCAL_EAR, включить
   USE_AFE_WAKE_WORD + SR_WN_WN10_RU_VALERA, выключить SR_WN_WN10_RU_PRIVETESP.
5. Пересобрать прошивку. Не вызывать erase_flash.

Пока каталога нет, сборка с SR_WN_WN10_RU_VALERA=y остановится.
