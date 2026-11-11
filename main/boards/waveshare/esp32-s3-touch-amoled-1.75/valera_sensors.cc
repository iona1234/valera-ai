#include "valera_sensors.h"

#include "board.h"
#include "config.h"

#include <cJSON.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

#define TAG "ValeraSensors"

namespace {

constexpr int kBarnaulLatE6 = 53360600;
constexpr int kBarnaulLonE6 = 83763600;

std::mutex g_mu;
ValeraClimate g_climate;
bool g_started = false;

int RoundToInt(float v) {
    return (int)(v >= 0.f ? v + 0.5f : v - 0.5f);
}

const char* ConditionFromCode(int code) {
    if (code == 0 || code == 1) return "ясно";
    if (code == 2 || code == 3) return "облачно";
    if (code == 45 || code == 48) return "туман";
    if (code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86) return "снег";
    if (code == 95 || code == 96 || code == 99) return "гроза";
    if (code == 51 || code == 53 || code == 55 || code == 56 || code == 57 || code == 61 ||
        code == 63 || code == 65 || code == 66 || code == 67 || code == 80 || code == 81 ||
        code == 82) {
        return "дождь";
    }
    return "погода";
}

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_75
// GPIO16/17 are free on the standard 1.75. On 1.75C GPIO16 is I2S MCLK.
constexpr gpio_num_t kBmeSda = GPIO_NUM_16;
constexpr gpio_num_t kBmeScl = GPIO_NUM_17;

class Bme280 {
public:
    bool Open() {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = I2C_NUM_1;
        bus_cfg.sda_io_num = kBmeSda;
        bus_cfg.scl_io_num = kBmeScl;
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = 1;
        if (i2c_new_master_bus(&bus_cfg, &bus_) != ESP_OK) {
            ESP_LOGW(TAG, "BME280 I2C bus failed");
            bus_ = nullptr;
            return false;
        }

        const uint8_t addrs[] = {0x76, 0x77};
        for (uint8_t addr : addrs) {
            if (i2c_master_probe(bus_, addr, 50) != ESP_OK) continue;
            i2c_device_config_t dev_cfg = {};
            dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            dev_cfg.device_address = addr;
            dev_cfg.scl_speed_hz = 100000;
            if (i2c_master_bus_add_device(bus_, &dev_cfg, &dev_) != ESP_OK) continue;
            uint8_t id = 0;
            if (!ReadReg(0xD0, &id, 1) || id != 0x60) {
                ESP_LOGW(TAG, "I2C 0x%02x is not BME280 (id 0x%02x)", addr, id);
                i2c_master_bus_rm_device(dev_);
                dev_ = nullptr;
                continue;
            }
            if (!LoadCalibration()) {
                i2c_master_bus_rm_device(dev_);
                dev_ = nullptr;
                continue;
            }
            WriteReg(0xF2, 0x01);
            ESP_LOGI(TAG, "BME280 at 0x%02x on GPIO16/17", addr);
            return true;
        }
        ESP_LOGW(TAG, "BME280 not found, face keeps running");
        return false;
    }

    bool Read(float* temp_c, float* rh) {
        if (dev_ == nullptr) return false;
        if (!WriteReg(0xF4, 0x25)) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
        uint8_t raw[8] = {};
        if (!ReadReg(0xF7, raw, sizeof(raw))) return false;
        int32_t adc_t = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
        int32_t adc_h = ((int32_t)raw[6] << 8) | raw[7];
        if (adc_t == 0x80000) return false;
        *temp_c = CompensateTemp(adc_t);
        *rh = CompensateHum(adc_h);
        return true;
    }

private:
    static uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
    static int16_t I16(const uint8_t* p) { return (int16_t)U16(p); }

    bool WriteReg(uint8_t reg, uint8_t val) {
        uint8_t buf[2] = {reg, val};
        return i2c_master_transmit(dev_, buf, 2, 100) == ESP_OK;
    }

    bool ReadReg(uint8_t reg, uint8_t* dst, size_t n) {
        return i2c_master_transmit_receive(dev_, &reg, 1, dst, n, 100) == ESP_OK;
    }

    bool LoadCalibration() {
        uint8_t tp[26] = {};
        uint8_t h[7] = {};
        if (!ReadReg(0x88, tp, sizeof(tp)) || !ReadReg(0xE1, h, sizeof(h))) return false;
        dig_t1_ = U16(tp);
        dig_t2_ = I16(tp + 2);
        dig_t3_ = I16(tp + 4);
        dig_h1_ = tp[25];
        dig_h2_ = I16(h);
        dig_h3_ = h[2];
        dig_h4_ = (int16_t)((h[3] << 4) | (h[4] & 0x0F));
        dig_h5_ = (int16_t)((h[5] << 4) | (h[4] >> 4));
        dig_h6_ = (int8_t)h[6];
        return true;
    }

    float CompensateTemp(int32_t adc_t) {
        double var1 = (((double)adc_t) / 16384.0 - ((double)dig_t1_) / 1024.0) * (double)dig_t2_;
        double var2 = ((((double)adc_t) / 131072.0 - ((double)dig_t1_) / 8192.0) *
                       (((double)adc_t) / 131072.0 - ((double)dig_t1_) / 8192.0)) *
                      (double)dig_t3_;
        t_fine_ = (int32_t)(var1 + var2);
        return (float)((var1 + var2) / 5120.0);
    }

    float CompensateHum(int32_t adc_h) {
        double var_h = ((double)t_fine_) - 76800.0;
        var_h = (adc_h - (((double)dig_h4_) * 64.0 + ((double)dig_h5_) / 16384.0 * var_h)) *
                (((double)dig_h2_) / 65536.0 *
                 (1.0 + ((double)dig_h6_) / 67108864.0 * var_h *
                            (1.0 + ((double)dig_h3_) / 67108864.0 * var_h)));
        var_h = var_h * (1.0 - ((double)dig_h1_) * var_h / 524288.0);
        if (var_h > 100.0) var_h = 100.0;
        if (var_h < 0.0) var_h = 0.0;
        return (float)var_h;
    }

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    uint16_t dig_t1_ = 0;
    int16_t dig_t2_ = 0;
    int16_t dig_t3_ = 0;
    uint8_t dig_h1_ = 0;
    int16_t dig_h2_ = 0;
    uint8_t dig_h3_ = 0;
    int16_t dig_h4_ = 0;
    int16_t dig_h5_ = 0;
    int8_t dig_h6_ = 0;
    int32_t t_fine_ = 0;
};

Bme280 g_bme;
bool g_bme_open = false;
#else
bool g_bme_warned = false;
#endif

void ApplyClockFix(const char* iso_local) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    if (iso_local == nullptr ||
        std::sscanf(iso_local, "%d-%d-%dT%d:%d", &year, &month, &day, &hour, &minute) != 5) {
        return;
    }
    struct tm wall = {};
    wall.tm_year = year - 1900;
    wall.tm_mon = month - 1;
    wall.tm_mday = day;
    wall.tm_hour = hour;
    wall.tm_min = minute;
    wall.tm_isdst = 0;
    time_t labeled = timegm(&wall);
    time_t now = time(nullptr);
    if (labeled <= 0 || now <= 0) return;
    std::lock_guard<std::mutex> lock(g_mu);
    g_climate.clock_fix_sec = (int)(labeled - now);
    g_climate.clock_fix_ok = true;
}

void FetchWeather() {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) return;
    auto http = network->CreateHttp(0);
    if (!http) return;
    http->SetHeader("User-Agent", "Valera/1.0");
    http->SetHeader("Accept", "application/json");

    char url[256];
    std::snprintf(url, sizeof(url),
                  "https://api.open-meteo.com/v1/forecast?latitude=%d.%06d&longitude=%d.%06d"
                  "&current=temperature_2m,relative_humidity_2m,weather_code"
                  "&timezone=Asia%%2FBarnaul",
                  kBarnaulLatE6 / 1000000, kBarnaulLatE6 % 1000000, kBarnaulLonE6 / 1000000,
                  kBarnaulLonE6 % 1000000);

    if (auto opened = http->Open("GET", url); !opened) {
        ESP_LOGW(TAG, "Open-Meteo open failed");
        return;
    }
    auto status = http->GetStatusCode();
    std::string body = http->ReadAll();
    http->Close();
    if (!status || *status != 200 || body.empty()) {
        ESP_LOGW(TAG, "Open-Meteo status failed");
        return;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) return;
    cJSON* current = cJSON_GetObjectItem(root, "current");
    if (cJSON_IsObject(current)) {
        cJSON* temp = cJSON_GetObjectItem(current, "temperature_2m");
        cJSON* hum = cJSON_GetObjectItem(current, "relative_humidity_2m");
        cJSON* code = cJSON_GetObjectItem(current, "weather_code");
        cJSON* when = cJSON_GetObjectItem(current, "time");
        if (cJSON_IsNumber(temp)) {
            std::lock_guard<std::mutex> lock(g_mu);
            g_climate.outdoor_ok = true;
            g_climate.outdoor_c = (float)temp->valuedouble;
            if (cJSON_IsNumber(hum)) g_climate.outdoor_rh = hum->valueint;
            if (cJSON_IsNumber(code)) {
                g_climate.weather_code = code->valueint;
                std::snprintf(g_climate.condition, sizeof(g_climate.condition), "%s",
                              ConditionFromCode(code->valueint));
            }
        }
        if (cJSON_IsString(when)) ApplyClockFix(when->valuestring);
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Barnaul weather updated");
}

void SensorTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(5000));
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_75
    g_bme_open = g_bme.Open();
#else
    if (!g_bme_warned) {
        ESP_LOGW(TAG, "BME280 disabled: GPIO16 is MCLK on 1.75C");
        g_bme_warned = true;
    }
#endif
    int weather_ticks = 90;
    for (;;) {
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_75
        if (g_bme_open) {
            float t = 0, rh = 0;
            bool ok = g_bme.Read(&t, &rh);
            std::lock_guard<std::mutex> lock(g_mu);
            g_climate.room_ok = ok;
            if (ok) {
                g_climate.room_c = t;
                g_climate.room_rh = rh;
            }
        }
#endif
        if (weather_ticks >= 90) {
            FetchWeather();
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                ok = g_climate.outdoor_ok;
            }
            weather_ticks = ok ? 0 : 80;
        }
        weather_ticks++;
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

}  // namespace

void ValeraSensors::Start() {
    if (g_started) return;
    g_started = true;
    xTaskCreate(SensorTask, "valera_env", 12288, nullptr, 2, nullptr);
}

ValeraClimate ValeraSensors::Get() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_climate;
}

std::string ValeraSensors::Describe() {
    ValeraClimate c = Get();
    char buf[192];
    if (c.room_ok && c.outdoor_ok) {
        std::snprintf(buf, sizeof(buf), "комната %d° %d%%, Барнаул %d° %d%% %s",
                      RoundToInt(c.room_c), RoundToInt(c.room_rh), RoundToInt(c.outdoor_c),
                      c.outdoor_rh, c.condition);
    } else if (c.outdoor_ok) {
        std::snprintf(buf, sizeof(buf), "BME280 нет, Барнаул %d° %d%% %s", RoundToInt(c.outdoor_c),
                      c.outdoor_rh, c.condition);
    } else if (c.room_ok) {
        std::snprintf(buf, sizeof(buf), "комната %d° %d%%, улица ещё не загружена",
                      RoundToInt(c.room_c), RoundToInt(c.room_rh));
    } else {
        std::snprintf(buf, sizeof(buf), "погода ещё не загружена");
    }
    return buf;
}
