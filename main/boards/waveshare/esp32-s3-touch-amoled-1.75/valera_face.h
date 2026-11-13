#pragma once

#include "display/lcd_display.h"

// Round 466x466 AMOLED face: thick readable clock + minimal Sber/RoboEyes-style
// capsule eyes. No tiny labels. 16 ms LVGL timer; never DisplayLockGuard in Tick.
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
    void UpdateStatusBar(bool update_all = false) override;
    void SetPowerSaveMode(bool on) override;
    void SetTheme(Theme* theme) override;

    static void RounderEventCb(lv_event_t* e);

private:
    enum class FaceMode { kIdle, kListen, kSpeak, kThink, kWifi, kSleep };

    struct Eye {
        lv_obj_t* body = nullptr;   // solid capsule
        lv_obj_t* pupil = nullptr;  // dark pupil only
        lv_obj_t* shine = nullptr;  // one catchlight
        int16_t cx = 0;
        int16_t cy = 0;
    };

    static void TickThunk(lv_timer_t* timer);
    void Tick();
    void BuildFace();
    void BuildEye(Eye* eye, int16_t cx, int16_t cy);
    void ApplyMode(FaceMode mode);
    void PaintDigits(const struct tm* tm, bool colon_on);
    void HideStockChrome();
    void AnimateAmbient();
    void AnimateEyes();
    void AnimateBars();
    void PickGazeTarget();
    void PlacePupil(Eye* eye, float look_x, float look_y, float open);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* bezel_ = nullptr;
    lv_obj_t* seg_[4][7] = {};
    lv_obj_t* colon_[2] = {};
    Eye left_{};
    Eye right_{};
    lv_obj_t* bars_[6] = {};
    lv_timer_t* timer_ = nullptr;

    bool face_ready_ = false;
    bool sleepy_ = false;
    int tick_ = 0;
    int last_hour_ = -1;
    int last_min_ = -1;
    bool last_colon_ = false;
    FaceMode mode_ = FaceMode::kIdle;

    int16_t eye_w_ = 88;
    int16_t eye_h_ = 56;
    int16_t pupil_r_ = 14;
    int16_t eye_cy_ = 310;
    int16_t open_h_ = 56;

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
