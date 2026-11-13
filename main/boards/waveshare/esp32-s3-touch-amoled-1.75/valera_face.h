#pragma once

#include "display/lcd_display.h"

// Round AMOLED face: large 7-seg clock + expressive oval eyes.
// No tiny labels. Layout is centered on 466x466. 16 ms LVGL timer;
// never take DisplayLockGuard from the timer callback.
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
        lv_obj_t* socket = nullptr;
        lv_obj_t* sclera = nullptr;
        lv_obj_t* iris_outer = nullptr;
        lv_obj_t* iris_inner = nullptr;
        lv_obj_t* pupil = nullptr;
        lv_obj_t* shine_big = nullptr;
        lv_obj_t* shine_small = nullptr;
        lv_obj_t* lid_top = nullptr;
        lv_obj_t* lid_bot = nullptr;
        lv_obj_t* halo = nullptr;
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
    void PlaceIris(Eye* eye, float look_x, float look_y);
    void PlaceLids(Eye* eye, float close_amount);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* bezel_ = nullptr;
    lv_obj_t* glow_ = nullptr;
    lv_obj_t* seg_[4][7] = {};
    lv_obj_t* colon_[2] = {};
    Eye left_{};
    Eye right_{};
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* bars_[8] = {};
    lv_timer_t* timer_ = nullptr;

    bool face_ready_ = false;
    bool sleepy_ = false;
    int tick_ = 0;
    int last_hour_ = -1;
    int last_min_ = -1;
    bool last_colon_ = false;
    FaceMode mode_ = FaceMode::kIdle;

    // Eye geometry (filled in BuildFace from screen size).
    int16_t eye_rx_ = 72;
    int16_t eye_ry_ = 60;
    int16_t iris_r_ = 40;
    int16_t pupil_r_ = 18;
    int16_t eye_cy_ = 300;

    float look_x_ = 0.f;
    float look_y_ = 0.f;
    float target_x_ = 0.f;
    float target_y_ = 0.f;
    int next_gaze_tick_ = 90;
    int next_blink_tick_ = 120;
    int blink_phase_ = 0;  // 0 idle, 1 closing, 2 opening
    int blink_tick_ = 0;
    float blink_ = 0.f;
    float squint_ = 0.f;
};
