#pragma once

#include "display/lcd_display.h"

#include <cstdint>

// Valera round face, rewritten as exclusive full-screen pages.
// Swipe left/right: Clock, Weather, Radio, Stations.
// Swipe down: Settings (brightness, volume, activation code).
// Activation code is its own full-screen page. Nothing else is drawn under it.
// No eyes, EQ bars, shade drag, or per-frame animation.
class ValeraLcdDisplay : public SpiLcdDisplay {
public:
    ValeraLcdDisplay(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t panel_handle,
                     int width, int height, int offset_x, int offset_y, bool mirror_x,
                     bool mirror_y, bool swap_xy);

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetHideSubtitle(bool hide) override;
    void UpdateStatusBar(bool update_all = false) override;
    void SetPowerSaveMode(bool on) override;
    void SetTheme(Theme* theme) override;

    void SetClockStyle(int look, int scale_pct, uint32_t hh_color, uint32_t mm_color);
    void LoadClockStyle();
    void OpenQuickShade();

    static void RounderEventCb(lv_event_t* e);

private:
    enum class Page { kClock, kWeather, kRadio, kStations, kSettings, kCode, kStatus, kCount };

    static void TickThunk(lv_timer_t* timer);
    static void GestureCb(lv_event_t* e);
    static void ButtonCb(lv_event_t* e);

    void Tick();
    void Build();
    void HideStockChrome();
    void ShowPage(Page page);
    void StepCarousel(int dir);
    void OnGesture(lv_event_t* e);
    void OnButton(int id);

    void PaintClock();
    void PaintWeather();
    void PaintRadio();
    void PaintStations();
    void PaintSettings();
    void PaintCode();
    void PaintStatus(const char* text);

    void ReloadActivation();
    void CaptureCode(const char* text);
    void NudgeBrightness(int delta);
    void NudgeVolume(int delta);
    void StepStation(int delta);
    const lv_font_t* Font() const;

    lv_obj_t* MakePage();
    lv_obj_t* MakeLabel(lv_obj_t* parent, lv_color_t color);
    lv_obj_t* MakeButton(lv_obj_t* parent, int w, int h, const char* text, int id);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* page_[(int)Page::kCount] = {};

    lv_obj_t* clock_time_ = nullptr;
    lv_obj_t* clock_date_ = nullptr;
    lv_obj_t* clock_foot_ = nullptr;

    lv_obj_t* weather_temp_ = nullptr;
    lv_obj_t* weather_cond_ = nullptr;
    lv_obj_t* weather_room_ = nullptr;

    lv_obj_t* radio_name_ = nullptr;
    lv_obj_t* radio_meta_ = nullptr;

    lv_obj_t* station_index_ = nullptr;
    lv_obj_t* station_name_ = nullptr;
    lv_obj_t* station_meta_ = nullptr;

    lv_obj_t* set_bri_ = nullptr;
    lv_obj_t* set_vol_ = nullptr;

    lv_obj_t* code_digits_ = nullptr;
    lv_obj_t* code_hint_ = nullptr;
    lv_obj_t* status_label_ = nullptr;

    lv_timer_t* timer_ = nullptr;
    Page page_ = Page::kClock;
    Page return_page_ = Page::kClock;

    bool face_ready_ = false;
    bool gesture_down_ = false;
    int16_t gesture_x_ = 0;
    int16_t gesture_y_ = 0;

    char activation_code_[16] = {};
    bool activation_bound_ = false;
    int station_idx_ = 0;
    int tick_ = 0;
    int brightness_ = 70;
    int volume_ = 70;
    uint32_t clock_color_ = 0xFFFFFF;
};
