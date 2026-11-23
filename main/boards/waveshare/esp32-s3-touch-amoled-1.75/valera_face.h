#pragma once

#include "display/lcd_display.h"

#include <cstdint>

// Round 466x466 Valera face.
// Idle: Popka Stack clock <-> weather, occasional Sber-like play eyes.
// Listen: cyan eyes + symmetric living mouth EQ (no text).
// Think: violet eyes + softer mouth EQ.
// Speak/reply: frameless text + coral living voice bars (different UI from listen).
// Wi-Fi: hotspot provisioning instructions (XiaoZhi AP).
// 16 ms LVGL timer; no DisplayLockGuard in Tick.
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

    // Clock look: 0=Modern/Stack, 1=Neon, 2=Soft, 3=Retro, 4=Digital.
    // Scale percent 80..160.
    void SetClockStyle(int look, int scale_pct, uint32_t hh_color, uint32_t mm_color);
    void LoadClockStyle();
    void OpenQuickShade() { ShowShade(true); }

    static void RounderEventCb(lv_event_t* e);

private:
    enum class ActiveMode { kIdle, kListen, kSpeak, kThink, kWifi, kSleep };
    enum class IdlePage { kClock, kWeather, kPlay, kSlides };

    static constexpr int kSpectrumN = 11;  // odd → perfect center bar, L↔R mirror

    struct Eye {
        lv_obj_t* body = nullptr;
        lv_obj_t* pupil = nullptr;
        lv_obj_t* shine = nullptr;
        int16_t home_cx = 0;
        int16_t home_cy = 0;
    };

    // Declared before methods that take SegDigit*.
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
    void PaintWifiHint();
    void PaintSlides();
    void ApplyClockStyleVisual();
    void SetGroupHidden(lv_obj_t* const* objs, int n, bool hidden);
    void BuildSegDigit(SegDigit* d, int cx, int cy, int dw, int dh, int thick);
    void SetSegDigit(SegDigit* d, int value, lv_color_t color);
    void ColorSegDigit(SegDigit* d, lv_color_t color);

    void BuildShade();
    void ShowShade(bool open);
    void SetShadePull(float pull);  // 0=closed … 1=open (finger / spring)
    void AnimateShadeTick();
    void RefreshShade();
    void CycleTheme();
    void HandleShadeTap(int16_t x, int16_t y);
    void AdvanceIdlePage();
    void RetreatIdlePage();
    static void GestureEventCb(lv_event_t* e);
    void OnGesture(lv_event_t* e);

    void AnimateAmbient();
    void AnimateEyes(Eye* left, Eye* right, int16_t w, int16_t h, int16_t pupil_r, bool playful);
    void AnimatePlayFx();
    void AnimateSpectrum(lv_obj_t* const* bars, float drive, float speed, lv_color_t c0,
                         lv_color_t c1);
    void AnimateReplyFx();
    void PickGazeTarget(float max_x, float max_y);
    void PlacePupil(Eye* eye, int16_t w, int16_t h, int16_t pupil_r, float look_x, float look_y,
                    float open);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* bezel_ = nullptr;

    // Popka-like Stack clock: sharp 7-segment digits (no bitmap zoom blur).
    SegDigit clock_hh_[2] = {};
    SegDigit clock_mm_[2] = {};
    lv_obj_t* clock_colon_[2] = {};
    lv_obj_t* time_hh_label_ = nullptr;  // unused legacy (kept null)
    lv_obj_t* time_mm_label_ = nullptr;
    lv_obj_t* clock_edge_weather_ = nullptr;
    lv_obj_t* clock_edge_date_ = nullptr;
    lv_obj_t* activation_label_ = nullptr;
    lv_obj_t* shade_grab_ = nullptr;
    lv_obj_t* clock_objs_[48] = {};
    int clock_obj_n_ = 0;
    int clock_look_ = 0;
    int clock_scale_pct_ = 100;
    uint32_t clock_hh_color_ = 0xFFFFFF;
    uint32_t clock_mm_color_ = 0xFF8FA3;
    char activation_code_[16] = {};
    bool boot_sound_played_ = false;

    // Weather: large readable labels (Popka-like).
    lv_obj_t* weather_glyph_ = nullptr;
    lv_obj_t* weather_out_label_ = nullptr;
    lv_obj_t* weather_room_label_ = nullptr;
    lv_obj_t* weather_cond_label_ = nullptr;
    lv_obj_t* weather_objs_[16] = {};
    int weather_obj_n_ = 0;

    // Procedural slideshow (0 flash image bytes).
    lv_obj_t* slide_wash_ = nullptr;
    lv_obj_t* slide_orb_[4] = {};
    lv_obj_t* slide_label_ = nullptr;
    lv_obj_t* slide_objs_[12] = {};
    int slide_obj_n_ = 0;
    int slide_index_ = 0;
    int slide_ticks_ = 0;

    Eye play_left_{};
    Eye play_right_{};
    lv_obj_t* play_orb_[10] = {};
    lv_obj_t* play_ring_ = nullptr;
    lv_obj_t* play_ring2_ = nullptr;
    lv_obj_t* play_objs_[28] = {};
    int play_obj_n_ = 0;
    int16_t play_eye_w_ = 120;
    int16_t play_eye_h_ = 74;
    int16_t play_pupil_r_ = 20;

    // Listen/think face: eyes + mouth spectrum.
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

    // Reply: text + coral voice EQ (same geometry as mouth, different color/role).
    lv_obj_t* reply_orb_[6] = {};
    lv_obj_t* voice_eq_[kSpectrumN] = {};
    lv_obj_t* caption_label_ = nullptr;
    lv_obj_t* reply_objs_[40] = {};
    int reply_obj_n_ = 0;
    int16_t voice_eq_cy_ = 390;

    // Wi-Fi hotspot screen.
    lv_obj_t* wifi_title_ = nullptr;
    lv_obj_t* wifi_ssid_ = nullptr;
    lv_obj_t* wifi_hint_ = nullptr;
    lv_obj_t* wifi_objs_[12] = {};
    int wifi_obj_n_ = 0;

    // Phone-like shade (swipe-down sheet, ~60 fps spring).
    lv_obj_t* shade_scrim_ = nullptr;
    lv_obj_t* shade_panel_ = nullptr;
    lv_obj_t* shade_handle_ = nullptr;
    lv_obj_t* shade_title_ = nullptr;
    lv_obj_t* shade_row_btn_[6] = {};
    lv_obj_t* shade_row_label_[6] = {};
    lv_obj_t* shade_row_value_[6] = {};
    lv_obj_t* shade_objs_[20] = {};
    int shade_obj_n_ = 0;
    int shade_panel_h_ = 0;
    float shade_pull_ = 0.f;     // visual 0..1
    float shade_target_ = 0.f;   // spring target
    bool shade_open_ = false;
    bool shade_dragging_ = false;
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

    // Idle pages only advance by swipe — no auto carousel (felt like reloads).
    static constexpr int kClockTicks = 24 * 60 * 60;
    static constexpr int kWeatherTicks = 24 * 60 * 60;
    static constexpr int kPlayTicks = 24 * 60 * 60;
    static constexpr int kSlidesTicks = 24 * 60 * 60;
};
