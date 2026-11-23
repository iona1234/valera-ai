#include "wifi_board.h"
#include "display/lcd_display.h"
#include "esp_lcd_co5300.h"

#include "codecs/box_audio_codec.h"
#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "config.h"
#include "power_save_timer.h"
#include "axp2101.h"
#include "i2c_device.h"
#include "valera_face.h"
#include "valera_radio_prefs.h"
#include "valera_radio_stations.h"
#include "valera_sensors.h"

#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include "esp_io_expander_tca9554.h"
#include "settings.h"

#include <ssid_manager.h>

#include <esp_lcd_touch_cst9217.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include <string>

#define TAG "WaveshareEsp32s3TouchAMOLED1inch75"

class Pmic : public Axp2101 {
public:
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        WriteReg(0x22, 0b110); // PWRON > OFFLEVEL as POWEROFF Source enable
        WriteReg(0x27, 0x10);  // hold 4s to power off

        // Disable All DCs but DC1
        WriteReg(0x80, 0x01);
        // Disable All LDOs
        WriteReg(0x90, 0x00);
        WriteReg(0x91, 0x00);

        // Set DC1 to 3.3V
        WriteReg(0x82, (3300 - 1500) / 100);

        // Set ALDO1 to 3.3V
        WriteReg(0x92, (3300 - 500) / 100);

        // Enable ALDO1(MIC)
        WriteReg(0x90, 0x01);

        WriteReg(0x64, 0x02); // CV charger voltage setting to 4.1V

        WriteReg(0x61, 0x02); // set Main battery precharge current to 50mA
        WriteReg(0x62, 0x08); // set Main battery charger current to 400mA ( 0x08-200mA, 0x09-300mA, 0x0A-400mA )
        WriteReg(0x63, 0x01); // set Main battery term charge current to 25mA
    }
};

#define LCD_OPCODE_WRITE_CMD (0x02ULL)
#define LCD_OPCODE_READ_CMD (0x03ULL)
#define LCD_OPCODE_WRITE_COLOR (0x32ULL)

static const co5300_lcd_init_cmd_t vendor_specific_init[] = {
    // set display to qspi mode
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},

    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

// Display UI: ValeraLcdDisplay (valera_face.*). Rounder is registered there.

class CustomBacklight : public Backlight {
public:
    CustomBacklight(esp_lcd_panel_io_handle_t panel_io) : Backlight(), panel_io_(panel_io) {}

protected:
    esp_lcd_panel_io_handle_t panel_io_;

    virtual void SetBrightnessImpl(uint8_t brightness) override {
        auto display = Board::GetInstance().GetDisplay();
        DisplayLockGuard lock(display);
        uint8_t data[1] = {((uint8_t)((255*  brightness) / 100))};
        int lcd_cmd = 0x51;
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
        esp_lcd_panel_io_tx_param(panel_io_, lcd_cmd, &data, sizeof(data));
    }
};

class WaveshareEsp32s3TouchAMOLED1inch75 : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Pmic* pmic_ = nullptr;
    Button boot_button_;
    ValeraLcdDisplay* display_;
    CustomBacklight* backlight_;
    esp_io_expander_handle_t io_expander = NULL;
    PowerSaveTimer* power_save_timer_;
    esp_timer_handle_t pwr_poll_timer_ = nullptr;
    bool pwr_pressed_ = false;
    int64_t pwr_press_ms_ = 0;
    bool pwr_long_fired_ = false;

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(20); });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness(); });
        power_save_timer_->OnShutdownRequest([this](){ 
            pmic_->PowerOff(); });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeTca9554(void) {
        esp_err_t ret = esp_io_expander_new_i2c_tca9554(i2c_bus_, I2C_ADDRESS, &io_expander);
        if (ret != ESP_OK)
            ESP_LOGE(TAG, "TCA9554 create returned error");
        ret = esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_INPUT);
        ESP_ERROR_CHECK(ret);
    }

    void InitializeAxp2101() {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(i2c_bus_, 0x34);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK;
        buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;
        buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;
        buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;
        buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;
        buscfg.max_transfer_sz = DISPLAY_WIDTH*  DISPLAY_HEIGHT*  sizeof(uint16_t);
        buscfg.flags = SPICOMMON_BUSFLAG_QUAD;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void BumpVolume(int delta) {
        auto codec = GetAudioCodec();
        if (codec == nullptr) return;
        int volume = codec->output_volume() + delta;
        if (volume > 100) volume = 20;  // wrap quiet→usable after max
        if (volume < 0) volume = 0;
        codec->SetOutputVolume(volume);
        GetDisplay()->ShowNotification(std::string(Lang::Strings::VOLUME) + std::to_string(volume));
    }

    void HandlePwrShortPress() {
        // Wake from sleep first; otherwise step volume.
        if (power_save_timer_) power_save_timer_->WakeUp();
        SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        BumpVolume(+10);
    }

    void HandlePwrLongPress() {
        ESP_LOGI(TAG, "PWR long press: soft PowerOff via AXP2101");
        if (display_) {
            display_->ClearChatMessages();
            display_->SetPowerSaveMode(true);
        }
        if (pmic_) pmic_->PowerOff();
    }

    static void PwrPollThunk(void* arg) {
        static_cast<WaveshareEsp32s3TouchAMOLED1inch75*>(arg)->PollPwrButton();
    }

    void PollPwrButton() {
        if (io_expander == nullptr) return;
        uint32_t level = 0;
        // SYS_OUT on EXIO4: high while PWR pressed (Waveshare HW reference).
        if (esp_io_expander_get_level(io_expander, IO_EXPANDER_PIN_NUM_4, &level) != ESP_OK) return;
        const bool down = level != 0;
        const int64_t now_ms = esp_timer_get_time() / 1000;

        if (down && !pwr_pressed_) {
            pwr_pressed_ = true;
            pwr_press_ms_ = now_ms;
            pwr_long_fired_ = false;
            return;
        }
        if (down && pwr_pressed_ && !pwr_long_fired_) {
            if (now_ms - pwr_press_ms_ >= 1800) {
                pwr_long_fired_ = true;
                HandlePwrLongPress();
            }
            return;
        }
        if (!down && pwr_pressed_) {
            const int64_t held = now_ms - pwr_press_ms_;
            pwr_pressed_ = false;
            if (!pwr_long_fired_ && held >= 40 && held < 1800) HandlePwrShortPress();
            pwr_long_fired_ = false;
        }
    }

    void InitializePwrButtonMonitor() {
        if (io_expander == nullptr) return;
        const esp_timer_create_args_t args = {
            .callback = &PwrPollThunk,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "valera_pwr",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&args, &pwr_poll_timer_) == ESP_OK) {
            esp_timer_start_periodic(pwr_poll_timer_, 50 * 1000);  // 50 ms
            ESP_LOGI(TAG, "PWR button monitor on TCA9554 EXIO4");
        }
    }

    void InitializeColdBootHygiene() {
        const esp_reset_reason_t reason = esp_reset_reason();
        ESP_LOGI(TAG, "Reset reason=%d (power-cycle clears RAM; NVS Wi-Fi kept)", (int)reason);
        // Cold / brownout / USB re-plug: start with a clean UI session.
        // Do NOT erase NVS — that would wipe Wi-Fi credentials every unplug.
        if (reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT) {
            if (display_) {
                display_->ClearChatMessages();
                display_->SetPowerSaveMode(false);
            }
            if (power_save_timer_) power_save_timer_->WakeUp();
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            // Short BOOT during Starting used to always open the hotspot.
            // On ESP32-S3 USB-Serial/JTAG, DTR/RTS often fakes a BOOT click and
            // knocked a working station back into AP mode. Only enter hotspot if
            // there is no saved SSID; otherwise treat as chat toggle after boot.
            if (app.GetDeviceState() == kDeviceStateStarting) {
                auto& ssids = SsidManager::GetInstance();
                if (ssids.GetSsidList().empty()) {
                    EnterWifiConfigMode();
                }
                return;
            }
            if (power_save_timer_) power_save_timer_->WakeUp();
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            ESP_LOGI(TAG, "BOOT long press: Wi-Fi reconfigure");
            EnterWifiConfigMode();
        });

#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_75
        InitializePwrButtonMonitor();
#endif
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;
        io_config.dc_gpio_num = GPIO_NUM_NC;
        io_config.spi_mode = 0;
        // 80 MHz QSPI helps keep partial LVGL flushes inside a 60 fps budget.
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 32;
        io_config.lcd_param_bits = 8;
        io_config.flags.quad_mode = true;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        const co5300_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(co5300_lcd_init_cmd_t),
            .flags = {
                .use_qspi_interface = 1,
            }};

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void* )&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(panel_io, &panel_config, &panel));
        esp_lcd_panel_set_gap(panel, 0x06, 0);
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, false);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new ValeraLcdDisplay(panel_io, panel,
                                        DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        backlight_ = new CustomBacklight(panel_io);
        backlight_->RestoreBrightness();
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH - 1,
            .y_max = DISPLAY_HEIGHT - 1,
            .rst_gpio_num = PIN_NUM_TOUCH_RST,
            .int_gpio_num = PIN_NUM_TOUCH_INT,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 1,
                .mirror_y = 1,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
        tp_io_config.scl_speed_hz = 400*  1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }

    // 初始化工具
    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "End this conversation and enter WiFi configuration mode.\n"
            "**CAUTION** You must ask the user to confirm this action.",
            PropertyList(), [this](const PropertyList& properties) {
                EnterWifiConfigMode();
                return true;
            });
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_75
        mcp_server.AddTool(
            "self.valera.climate",
            "Текущая погода Барнаула (улица) и показания комнатного BME280, если он подключён.",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue { return ValeraSensors::Describe(); });
        mcp_server.AddTool(
            "self.valera.clock_style",
            "Тема/стиль часов Valera. look: 0=Modern, 1=Neon, 2=Soft, 3=Retro, 4=Digital; "
            "scale_pct 80..160; hh_color/mm_color как 0xRRGGBB.",
            PropertyList({Property("look", kPropertyTypeInteger, 0, 0, 4),
                          Property("scale_pct", kPropertyTypeInteger, 100, 80, 160),
                          Property("hh_color", kPropertyTypeInteger, 0xFFFFFF),
                          Property("mm_color", kPropertyTypeInteger, 0xFF8FA3)}),
            [this](const PropertyList& properties) -> ReturnValue {
                auto* face = static_cast<ValeraLcdDisplay*>(display_);
                face->SetClockStyle(properties["look"].value<int>(),
                                    properties["scale_pct"].value<int>(),
                                    (uint32_t)properties["hh_color"].value<int>(),
                                    (uint32_t)properties["mm_color"].value<int>());
                return true;
            });
        mcp_server.AddTool(
            "self.valera.radio_stations",
            "Видимые российские интернет-радиостанции Valera (скрытые через radio_hide не показаны).",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                ValeraRadioPrefs::Load();
                return ValeraRadioPrefs::ListVisible();
            });
        mcp_server.AddTool(
            "self.valera.radio_hide",
            "Скрыть станцию из списка (нерабочая/нежелательная). index — номер из radio_stations.",
            PropertyList({Property("index", kPropertyTypeInteger, 0, 0, kValeraRadioStationCount - 1)}),
            [](const PropertyList& properties) -> ReturnValue {
                ValeraRadioPrefs::SetHidden(properties["index"].value<int>(), true);
                return ValeraRadioPrefs::ListHidden();
            });
        mcp_server.AddTool(
            "self.valera.radio_restore",
            "Вернуть скрытую станцию. index — номер станции; без параметров — восстановить все.",
            PropertyList({Property("index", kPropertyTypeInteger, -1, -1, kValeraRadioStationCount - 1)}),
            [](const PropertyList& properties) -> ReturnValue {
                const int idx = properties["index"].value<int>();
                if (idx < 0) ValeraRadioPrefs::ClearHidden();
                else ValeraRadioPrefs::SetHidden(idx, false);
                return ValeraRadioPrefs::ListVisible();
            });
        mcp_server.AddTool(
            "self.valera.radio_hidden",
            "Список скрытых радиостанций Valera.",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                return ValeraRadioPrefs::ListHidden();
            });
#endif
    }

public:
    WaveshareEsp32s3TouchAMOLED1inch75() : boot_button_(BOOT_BUTTON_GPIO, false, 3000) {
        InitializePowerSaveTimer();
        InitializeCodecI2c();
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_75
        InitializeTca9554();
#endif
        InitializeAxp2101();
        InitializeSpi();
        InitializeDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_AMOLED_1_75
        ValeraRadioPrefs::Load();
        ValeraSensors::Start();
#endif
        InitializeColdBootHygiene();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        return backlight_;
    }

    virtual bool GetBatteryLevel(int &level, bool &charging, bool &discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging)
        {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(WaveshareEsp32s3TouchAMOLED1inch75);
