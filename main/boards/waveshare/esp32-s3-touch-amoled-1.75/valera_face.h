#pragma once

#include "display/lcd_display.h"

// Round 466x466 Valera face.
// Idle: clock <-> weather, occasional Sber-like play eyes.
// Assistant face: large centered eyes + mouth spectrum (no text).
// Assistant reply: full-screen readable text + soft AMOLED effects (no eyes).
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

    static void RounderEventCb(lv_event_t* e);

private:
    enum class ActiveMode { kIdle, kListen, kSpeak, kThink, kWifi, kSleep };
    enum class IdlePage { kClock, kWeather, kPlay };

    struct Eye {
        lv_obj_t* body = nullptr;
        lv_obj_t* pupil = nullptr;
        lv_obj_t* shine = nullptr;
        int16_t home_cx = 0;
        int16_t home_cy = 0;
    };

    static void TickThunk(lv_timer_t* timer);
    void Tick();
    void BuildFace();
    void BuildEye(Eye* eye, int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t pupil_r);
    void HideStockChrome();

    void SetActiveMode(ActiveMode mode);
    void ShowIdlePage(IdlePage page);
    void AdvanceIdlePage();
    void LayoutForActive(bool active);
    void ApplyCaption();
    void SyncActiveStage();
    bool InReplyStage() const;

    void PaintClock(const struct tm* tm, bool colon_on);
    void PaintWeather();
    void SetGroupHidden(lv_obj_t* const* objs, int n, bool hidden);

    void AnimateAmbient();
    void AnimateEyes(Eye* left, Eye* right, int16_t w, int16_t h, int16_t pupil_r, bool playful);
    void AnimatePlayFx();
    void AnimateSpectrum();
    void AnimateReplyFx();
    void PickGazeTarget(float max_x, float max_y);
    void PlacePupil(Eye* eye, int16_t w, int16_t h, int16_t pupil_r, float look_x, float look_y,
                    float open);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* bezel_ = nullptr;

    lv_obj_t* clock_seg_[4][7] = {};
    lv_obj_t* clock_colon_[2] = {};
    lv_obj_t* clock_objs_[30] = {};
    int clock_obj_n_ = 0;

    lv_obj_t* weather_glyph_ = nullptr;
    lv_obj_t* weather_out_seg_[2][7] = {};
    lv_obj_t* weather_out_minus_ = nullptr;
    lv_obj_t* weather_out_dot_ = nullptr;
    lv_obj_t* weather_room_seg_[2][7] = {};
    lv_obj_t* weather_room_dot_ = nullptr;
    lv_obj_t* weather_objs_[40] = {};
    int weather_obj_n_ = 0;

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

    // Assistant face stage: eyes + mouth spectrum.
    Eye act_left_{};
    Eye act_right_{};
    lv_obj_t* spectrum_[12] = {};
    lv_obj_t* face_objs_[30] = {};
    int face_obj_n_ = 0;
    int16_t act_eye_w_ = 128;
    int16_t act_eye_h_ = 80;
    int16_t act_pupil_r_ = 22;
    int16_t act_eye_cy_ = 180;
    int16_t spectrum_base_y_ = 300;

    // Assistant reply stage: large text + soft ambient (no border frame).
    lv_obj_t* reply_ring_ = nullptr;
    lv_obj_t* reply_ring2_ = nullptr;
    lv_obj_t* reply_orb_[6] = {};
    lv_obj_t* caption_label_ = nullptr;
    lv_obj_t* reply_objs_[20] = {};
    int reply_obj_n_ = 0;

    lv_timer_t* timer_ = nullptr;

    bool face_ready_ = false;
    bool sleepy_ = false;
    bool assistant_active_ = false;
    bool caption_dirty_ = false;
    bool has_caption_ = false;
    char caption_text_[256] = {};
    int tick_ = 0;
    int idle_page_ticks_ = 0;
    int idle_step_ = 0;
    int last_hour_ = -1;
    int last_min_ = -1;
    bool last_colon_ = false;
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

    static constexpr int kClockTicks = 14 * 60;
    static constexpr int kWeatherTicks = 10 * 60;
    static constexpr int kPlayTicks = 12 * 60;
};
