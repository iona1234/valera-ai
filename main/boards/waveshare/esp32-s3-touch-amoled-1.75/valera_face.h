#pragma once

#include "display/lcd_display.h"

// Color watch face on the stock LVGL display. Audio stays on its own task.
// The ~16 ms (60 fps) timer runs on the LVGL thread and must not take
// DisplayLockGuard. Only dirty widgets are updated each tick.
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

    static void TickThunk(lv_timer_t* timer);
    void Tick();
    void BuildFace();
    void ApplyMode(FaceMode mode);
    void PaintDigits(const struct tm* tm, bool colon_on);
    void PaintInfo();
    void HideStockChrome();
    void AnimateEyes();
    void AnimateBars();
    void AnimateAmbient();
    void AnimateOrbits();
    void SetModeChip(const char* text, lv_color_t color);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* bezel_outer_ = nullptr;
    lv_obj_t* bezel_inner_ = nullptr;
    lv_obj_t* bezel_dot_[12] = {};
    lv_obj_t* glow_ring_ = nullptr;
    lv_obj_t* halo_[2] = {};
    lv_obj_t* halo_outer_[2] = {};
    lv_obj_t* seg_[4][7] = {};
    lv_obj_t* colon_[2] = {};
    lv_obj_t* eye_socket_[2] = {};
    lv_obj_t* eye_[2] = {};
    lv_obj_t* iris_[2] = {};
    lv_obj_t* pupil_[2] = {};
    lv_obj_t* shine_[2] = {};
    lv_obj_t* shine2_[2] = {};
    lv_obj_t* lid_[2] = {};
    lv_obj_t* lid_bot_[2] = {};
    lv_obj_t* orbit_[6] = {};
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* mode_chip_ = nullptr;
    lv_obj_t* out_pill_ = nullptr;
    lv_obj_t* room_pill_ = nullptr;
    lv_obj_t* out_dot_ = nullptr;
    lv_obj_t* room_dot_ = nullptr;
    lv_obj_t* out_label_ = nullptr;
    lv_obj_t* room_label_ = nullptr;
    lv_obj_t* caption_ = nullptr;
    lv_obj_t* bars_[12] = {};
    lv_timer_t* timer_ = nullptr;

    bool face_ready_ = false;
    bool sleepy_ = false;
    bool caption_dirty_ = false;
    char caption_text_[120] = {};
    int tick_ = 0;
    int last_hour_ = -1;
    int last_min_ = -1;
    bool last_colon_ = false;
    int last_eye_h_ = -1;
    int last_look_x_ = 999;
    int last_look_y_ = 999;
    int last_iris_ = -1;
    FaceMode mode_ = FaceMode::kIdle;
};
