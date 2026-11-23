#pragma once

#include "display/lcd_display.h"

#include <cstdint>

// Valera round face (466×466) — HaloWatch-inspired, LVGL + XiaoZhi.
// Idle carousel (swipe): Clock ↔ Weather ↔ Radio ↔ Stations.
// Shade: brightness ±, volume ±, Radio, Settings (activation code).
// Listen/speak: eyes + EQ bars only. No floating orbs.
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
    void OpenQuickShade() { ShowShade(true); }

    static void RounderEventCb(lv_event_t* e);

private:
    enum class ActiveMode { kIdle, kListen, kSpeak, kThink, kWifi, kSleep };
    enum class IdlePage { kClock, kWeather, kRadio, kStations };

    static constexpr int kSpectrumN = 11;
    static constexpr int kStationRowMax = 24;

    struct Eye {
        lv_obj_t* body = nullptr;
        lv_obj_t* pupil = nullptr;
        lv_obj_t* shine = nullptr;
        int16_t home_cx = 0;
        int16_t home_cy = 0;
    };

    struct SegDigit {
        lv_obj_t* seg[7] = {};
    };

    static void TickThunk(lv_timer_t* timer);
    void Tick();
    void BuildFace();
    void BuildEye(Eye* eye, int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t pupil_r);
    void BuildSpectrum(lv_obj_t** bars, lv_obj_t** track, int* track_n, int track_cap, int cy,
                       lv_color_t color);
    void HideStockChrome();

    void SetActiveMode(ActiveMode mode);
    void ShowIdlePage(IdlePage page);
    void LayoutForActive(bool active);
    void ApplyCaption();
    void SyncActiveStage();
    bool InReplyStage() const;
    bool InWifiStage() const;

    void PaintClock(const struct tm* tm, bool colon_on);
    void PaintWeather();
    void PaintRadio();
    void PaintWifiHint();
    void ApplyClockStyleVisual();
    void SetGroupHidden(lv_obj_t* const* objs, int n, bool hidden);
    void BuildSegDigit(SegDigit* d, int cx, int cy, int dw, int dh, int thick);
    void SetSegDigit(SegDigit* d, int value, lv_color_t color);
    void ColorSegDigit(SegDigit* d, lv_color_t color);

    void BuildShade();
    void BuildSettings();
    void BuildRadio();
    void BuildStations();
    void ShowShade(bool open);
    void ShowSettings(bool open);
    void OpenRadio();
    void OpenStations();
    void SelectStation(int index);
    void SetShadePull(float pull);
    void AnimateShadeTick();
    void RefreshShade();
    void RefreshSettings();
    void CycleTheme();
    void NudgeBrightness(int delta);
    void NudgeVolume(int delta);
    void AdvanceIdlePage();
    void RetreatIdlePage();

    static void GestureEventCb(lv_event_t* e);
    static void ShadeBtnEventCb(lv_event_t* e);
    static void SettingsBtnEventCb(lv_event_t* e);
    static void RadioBtnEventCb(lv_event_t* e);
    static void StationRowEventCb(lv_event_t* e);
    void OnGesture(lv_event_t* e);
    void OnShadeBtn(lv_event_t* e);
    void OnSettingsBtn(lv_event_t* e);
    void OnRadioBtn(lv_event_t* e);
    void OnStationRow(lv_event_t* e);

    void AnimateAmbient();
    void AnimateEyes(Eye* left, Eye* right, int16_t w, int16_t h, int16_t pupil_r, bool playful);
    void AnimateSpectrum(lv_obj_t* const* bars, int cy, float drive, float speed, lv_color_t c0,
                         lv_color_t c1);
    void PickGazeTarget(float max_x, float max_y);
    void PlacePupil(Eye* eye, int16_t w, int16_t h, int16_t pupil_r, float look_x, float look_y,
                    float open);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* bezel_ = nullptr;

    SegDigit clock_hh_[2] = {};
    SegDigit clock_mm_[2] = {};
    lv_obj_t* clock_colon_[2] = {};
    lv_obj_t* clock_edge_weather_ = nullptr;
    lv_obj_t* clock_edge_date_ = nullptr;
    lv_obj_t* clock_wx_pill_ = nullptr;
    lv_obj_t* shade_grab_ = nullptr;
    lv_obj_t* clock_objs_[48] = {};
    int clock_obj_n_ = 0;
    int clock_look_ = 0;
    int clock_scale_pct_ = 100;
    uint32_t clock_hh_color_ = 0xFFFFFF;
    uint32_t clock_mm_color_ = 0xFF8FA3;
    char activation_code_[16] = {};
    bool activation_bound_ = false;
    int last_seg_hh_[2] = {-1, -1};
    int last_seg_mm_[2] = {-1, -1};
    bool boot_sound_played_ = false;

    lv_obj_t* weather_glyph_ = nullptr;
    lv_obj_t* weather_out_label_ = nullptr;
    lv_obj_t* weather_room_label_ = nullptr;
    lv_obj_t* weather_cond_label_ = nullptr;
    lv_obj_t* weather_objs_[16] = {};
    int weather_obj_n_ = 0;

    Eye act_left_{};
    Eye act_right_{};
    lv_obj_t* spectrum_[kSpectrumN] = {};
    lv_obj_t* face_objs_[40] = {};
    int face_obj_n_ = 0;
    int16_t act_eye_w_ = 128;
    int16_t act_eye_h_ = 80;
    int16_t act_pupil_r_ = 22;
    int16_t act_eye_cy_ = 180;
    int16_t spectrum_center_y_ = 320;

    lv_obj_t* voice_eq_[kSpectrumN] = {};
    lv_obj_t* caption_label_ = nullptr;
    lv_obj_t* reply_objs_[40] = {};
    int reply_obj_n_ = 0;
    int16_t voice_eq_cy_ = 390;

    lv_obj_t* wifi_title_ = nullptr;
    lv_obj_t* wifi_ssid_ = nullptr;
    lv_obj_t* wifi_hint_ = nullptr;
    lv_obj_t* wifi_objs_[12] = {};
    int wifi_obj_n_ = 0;

    // Control Center shade (no circular clip — cheap + stable).
    lv_obj_t* shade_panel_ = nullptr;
    lv_obj_t* shade_handle_ = nullptr;
    lv_obj_t* shade_title_ = nullptr;
    lv_obj_t* shade_bri_minus_ = nullptr;
    lv_obj_t* shade_bri_plus_ = nullptr;
    lv_obj_t* shade_bri_val_ = nullptr;
    lv_obj_t* shade_vol_minus_ = nullptr;
    lv_obj_t* shade_vol_plus_ = nullptr;
    lv_obj_t* shade_vol_val_ = nullptr;
    lv_obj_t* shade_settings_btn_ = nullptr;
    lv_obj_t* shade_radio_btn_ = nullptr;
    lv_obj_t* shade_theme_btn_ = nullptr;
    lv_obj_t* shade_wifi_btn_ = nullptr;
    int shade_panel_h_ = 0;
    float shade_pull_ = 0.f;
    float shade_target_ = 0.f;
    bool shade_open_ = false;
    bool shade_dragging_ = false;
    bool settings_open_ = false;

    // Settings page: activation code + status + radio entry.
    lv_obj_t* settings_panel_ = nullptr;
    lv_obj_t* settings_title_ = nullptr;
    lv_obj_t* settings_code_ = nullptr;
    lv_obj_t* settings_wifi_ = nullptr;
    lv_obj_t* settings_hint_ = nullptr;
    lv_obj_t* settings_radio_btn_ = nullptr;
    lv_obj_t* settings_back_ = nullptr;

    // Radio poster + stations carousel list.
    lv_obj_t* radio_title_ = nullptr;
    lv_obj_t* radio_station_ = nullptr;
    lv_obj_t* radio_meta_ = nullptr;
    lv_obj_t* radio_play_btn_ = nullptr;
    lv_obj_t* radio_list_btn_ = nullptr;
    lv_obj_t* radio_eq_[kSpectrumN] = {};
    lv_obj_t* radio_objs_[40] = {};
    int radio_obj_n_ = 0;
    int16_t radio_eq_cy_ = 300;
    int radio_station_idx_ = 0;
    bool radio_playing_ = false;

    lv_obj_t* stations_title_ = nullptr;
    lv_obj_t* stations_list_ = nullptr;
    lv_obj_t* stations_rows_[kStationRowMax] = {};
    lv_obj_t* stations_objs_[12] = {};
    int stations_obj_n_ = 0;

    bool gesture_tracking_ = false;
    bool gesture_from_top_ = false;
    int16_t gesture_start_x_ = 0;
    int16_t gesture_start_y_ = 0;
    int16_t gesture_last_y_ = 0;

    lv_timer_t* timer_ = nullptr;

    bool face_ready_ = false;
    bool sleepy_ = false;
    bool assistant_active_ = false;
    bool caption_dirty_ = false;
    bool has_caption_ = false;
    char caption_text_[384] = {};
    char emotion_name_[32] = "neutral";
    int tick_ = 0;
    int idle_page_ticks_ = 0;
    int idle_step_ = 0;
    int last_hour_ = -1;
    int last_min_ = -1;
    int last_wday_ = -1;
    int last_mday_ = -1;
    int last_out_temp_ = 9999;
    int last_room_temp_ = 9999;
    int last_weather_code_ = -999;

    ActiveMode active_mode_ = ActiveMode::kIdle;
    IdlePage idle_page_ = IdlePage::kClock;

    float look_x_ = 0.f;
    float look_y_ = 0.f;
    float target_x_ = 0.f;
    float target_y_ = 0.f;
    int next_gaze_tick_ = 90;
    int next_blink_tick_ = 120;
    int blink_phase_ = 0;
    int blink_tick_ = 0;
    float blink_ = 0.f;
    float squint_ = 0.f;
};
