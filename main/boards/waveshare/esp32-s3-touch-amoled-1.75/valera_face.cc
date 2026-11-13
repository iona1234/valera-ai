#include "valera_face.h"

#include "application.h"
#include "device_state.h"
#include "valera_sensors.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define TAG "ValeraFace"

namespace {

// AMOLED palette: thick cyan digits, soft white eyes (Sber / 宗主 style).
const lv_color_t kBg = lv_color_hex(0x000000);
const lv_color_t kCyan = lv_color_hex(0x00E8FF);
const lv_color_t kCyanDim = lv_color_hex(0x004A55);
const lv_color_t kMagenta = lv_color_hex(0xFF4DB8);
const lv_color_t kThink = lv_color_hex(0x8FA6FF);
const lv_color_t kEye = lv_color_hex(0xF2F6FA);
const lv_color_t kPupil = lv_color_hex(0x0A0C10);
const lv_color_t kSegOff = lv_color_hex(0x0A1218);
const lv_color_t kWhite = lv_color_hex(0xFFFFFF);

constexpr uint8_t kSegBits[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

lv_obj_t* MakePlain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* MakeSeg(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* s = MakePlain(parent);
    lv_obj_set_size(s, w, h);
    lv_obj_set_pos(s, x, y);
    lv_obj_set_style_radius(s, (w < h ? w : h) / 2, 0);
    lv_obj_set_style_bg_color(s, kSegOff, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    return s;
}

float Clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float Smoothstep(float t) {
    t = Clampf(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float Frand(float lo, float hi) {
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

}  // namespace

ValeraLcdDisplay::ValeraLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                                   esp_lcd_panel_handle_t panel_handle, int width, int height,
                                   int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                   bool swap_xy)
    : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

void ValeraLcdDisplay::RounderEventCb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

void ValeraLcdDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();
    DisplayLockGuard lock(this);
    lv_display_add_event_cb(display_, RounderEventCb, LV_EVENT_INVALIDATE_AREA, NULL);
    HideStockChrome();
    BuildFace();
    face_ready_ = true;
    timer_ = lv_timer_create(TickThunk, 16, this);
}

void ValeraLcdDisplay::HideStockChrome() {
    auto hide = [](lv_obj_t* o) {
        if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    hide(status_bar_);
    hide(top_bar_);
    hide(bottom_bar_);
    hide(side_bar_);
    hide(emoji_box_);
    hide(emoji_label_);
    hide(emoji_image_);
    hide(chat_message_label_);
    hide(content_);
    if (container_) {
        lv_obj_set_style_bg_color(container_, kBg, 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    }
}

void ValeraLcdDisplay::BuildEye(Eye* eye, int16_t cx, int16_t cy) {
    eye->cx = cx;
    eye->cy = cy;

    // One solid capsule — Sber / RoboEyes / 宗主: expression from shape + pupil only.
    eye->body = MakePlain(root_);
    lv_obj_set_size(eye->body, eye_w_, eye_h_);
    lv_obj_set_pos(eye->body, cx - eye_w_ / 2, cy - eye_h_ / 2);
    lv_obj_set_style_radius(eye->body, eye_h_ / 2, 0);
    lv_obj_set_style_bg_color(eye->body, kEye, 0);
    lv_obj_set_style_bg_opa(eye->body, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(eye->body, true, 0);

    eye->pupil = MakePlain(eye->body);
    lv_obj_set_size(eye->pupil, pupil_r_ * 2, pupil_r_ * 2);
    lv_obj_set_style_radius(eye->pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye->pupil, kPupil, 0);
    lv_obj_set_style_bg_opa(eye->pupil, LV_OPA_COVER, 0);

    eye->shine = MakePlain(eye->body);
    lv_obj_set_size(eye->shine, 7, 7);
    lv_obj_set_style_radius(eye->shine, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye->shine, kWhite, 0);
    lv_obj_set_style_bg_opa(eye->shine, LV_OPA_80, 0);

    PlacePupil(eye, 0.f, 0.f, 1.f);
}

void ValeraLcdDisplay::BuildFace() {
    const int W = LV_HOR_RES;
    const int H = LV_VER_RES;
    const int min_side = W < H ? W : H;
    const int cx = W / 2;

    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, kBg, 0);

    root_ = MakePlain(screen);
    lv_obj_set_size(root_, W, H);
    lv_obj_set_style_bg_color(root_, kBg, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    // Thin quiet rim — opacity pulse only.
    bezel_ = MakePlain(root_);
    lv_obj_set_size(bezel_, min_side - 20, min_side - 20);
    lv_obj_center(bezel_);
    lv_obj_set_style_radius(bezel_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(bezel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bezel_, 2, 0);
    lv_obj_set_style_border_color(bezel_, kCyanDim, 0);
    lv_obj_set_style_border_opa(bezel_, LV_OPA_50, 0);

    // Compact eyes (RoboEyes proportions), under a thick clock.
    eye_w_ = (int16_t)(min_side * 0.19f);   // ~88 px
    eye_h_ = (int16_t)(eye_w_ * 0.62f);     // capsule, not giant circles
    if (eye_w_ < 78) eye_w_ = 78;
    if (eye_h_ < 48) eye_h_ = 48;
    pupil_r_ = (int16_t)(eye_h_ * 0.28f);
    open_h_ = eye_h_;

    // Thick 7-seg clock in the optical center-upper safe zone of the round panel.
    // Digits must read from arm's length on 1.75" — fat segments, generous gaps.
    const int digit_w = (int)(min_side * 0.155f);  // ~72
    const int digit_h = (int)(min_side * 0.28f);   // ~130
    const int thick = (int)(digit_w * 0.22f);       // ~16 — fat, not thin
    const int gap = (int)(digit_w * 0.16f);
    const int colon_gap = (int)(digit_w * 0.34f);
    const int total_w = digit_w * 4 + gap * 3 + colon_gap;
    const int origin_x = (W - total_w) / 2;
    // Sit below the top curve of the round bezel (~18% from top).
    const int origin_y = (int)(min_side * 0.155f);
    const int inset = thick / 2 + 1;

    int xs[4];
    xs[0] = origin_x;
    xs[1] = origin_x + digit_w + gap;
    xs[2] = xs[1] + digit_w + gap + colon_gap;
    xs[3] = xs[2] + digit_w + gap;

    for (int d = 0; d < 4; ++d) {
        const int x = xs[d];
        const int y = origin_y;
        seg_[d][0] = MakeSeg(root_, x + inset, y, digit_w - 2 * inset, thick);
        seg_[d][1] = MakeSeg(root_, x + digit_w - thick, y + inset, thick, digit_h / 2 - inset);
        seg_[d][2] =
            MakeSeg(root_, x + digit_w - thick, y + digit_h / 2, thick, digit_h / 2 - inset);
        seg_[d][3] = MakeSeg(root_, x + inset, y + digit_h - thick, digit_w - 2 * inset, thick);
        seg_[d][4] = MakeSeg(root_, x, y + digit_h / 2, thick, digit_h / 2 - inset);
        seg_[d][5] = MakeSeg(root_, x, y + inset, thick, digit_h / 2 - inset);
        seg_[d][6] =
            MakeSeg(root_, x + inset, y + digit_h / 2 - thick / 2, digit_w - 2 * inset, thick);
    }

    const int colon_x = xs[1] + digit_w + (colon_gap - thick) / 2;
    const int dot = thick;
    colon_[0] = MakeSeg(root_, colon_x, origin_y + digit_h / 3 - dot / 2, dot, dot);
    colon_[1] = MakeSeg(root_, colon_x, origin_y + (2 * digit_h) / 3 - dot / 2, dot, dot);
    lv_obj_set_style_bg_color(colon_[0], kCyan, 0);
    lv_obj_set_style_bg_color(colon_[1], kCyan, 0);
    lv_obj_set_style_radius(colon_[0], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(colon_[1], LV_RADIUS_CIRCLE, 0);

    // Eyes sit in the lower half with clear air under the clock.
    const int16_t gap_eyes = (int16_t)(eye_w_ * 0.28f);
    eye_cy_ = (int16_t)(origin_y + digit_h + (min_side - (origin_y + digit_h)) * 0.42f);
    const int16_t left_cx = (int16_t)(cx - eye_w_ / 2 - gap_eyes);
    const int16_t right_cx = (int16_t)(cx + eye_w_ / 2 + gap_eyes);
    BuildEye(&left_, left_cx, eye_cy_);
    BuildEye(&right_, right_cx, eye_cy_);

    // Quiet EQ under eyes for listen/speak only.
    for (int i = 0; i < 6; ++i) {
        bars_[i] = MakePlain(root_);
        lv_obj_set_size(bars_[i], 10, 14);
        lv_obj_set_pos(bars_[i], cx - 52 + i * 18, eye_cy_ + eye_h_ / 2 + 22);
        lv_obj_set_style_radius(bars_[i], 5, 0);
        lv_obj_set_style_bg_color(bars_[i], kCyan, 0);
        lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
    }

    PickGazeTarget();
    next_blink_tick_ = 100 + (rand() % 120);
    ApplyMode(FaceMode::kIdle);
}

void ValeraLcdDisplay::TickThunk(lv_timer_t* timer) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->Tick();
}

void ValeraLcdDisplay::PickGazeTarget() {
    const float max_x = eye_w_ * 0.18f;
    const float max_y = eye_h_ * 0.16f;
    target_x_ = Frand(-max_x, max_x);
    target_y_ = Frand(-max_y, max_y);
    next_gaze_tick_ = tick_ + 60 + (rand() % 110);
}

void ValeraLcdDisplay::PlacePupil(Eye* eye, float look_x, float look_y, float open) {
    const int h = (int)(open_h_ * Clampf(open, 0.08f, 1.f));
    lv_obj_set_height(eye->body, h);
    lv_obj_set_y(eye->body, eye->cy - h / 2);
    lv_obj_set_style_radius(eye->body, h / 2, 0);

    const float cx = (float)eye_w_ * 0.5f + look_x;
    const float cy = (float)h * 0.5f + look_y;
    const float lim_x = eye_w_ * 0.22f;
    const float lim_y = h * 0.18f;
    const int px = (int)Clampf(cx, pupil_r_ + 2.f, eye_w_ - pupil_r_ - 2.f);
    const int py = (int)Clampf(cy, pupil_r_ + 2.f, h - pupil_r_ - 2.f);
    (void)lim_x;
    (void)lim_y;
    lv_obj_set_pos(eye->pupil, px - pupil_r_, py - pupil_r_);
    lv_obj_set_pos(eye->shine, px - pupil_r_ - 3, py - pupil_r_ - 3);
}

void ValeraLcdDisplay::Tick() {
    if (!face_ready_ || sleepy_) return;
    ++tick_;

    FaceMode want = FaceMode::kIdle;
    switch (Application::GetInstance().GetDeviceState()) {
        case kDeviceStateListening:
            want = FaceMode::kListen;
            break;
        case kDeviceStateSpeaking:
            want = FaceMode::kSpeak;
            break;
        case kDeviceStateIdle:
            want = FaceMode::kIdle;
            break;
        case kDeviceStateConnecting:
        case kDeviceStateStarting:
        case kDeviceStateWifiConfiguring:
            want = FaceMode::kWifi;
            break;
        default:
            want = FaceMode::kThink;
            break;
    }
    if (want != mode_) ApplyMode(want);

    time_t now = time(nullptr);
    ValeraClimate climate = ValeraSensors::Get();
    if (climate.clock_fix_ok) now += climate.clock_fix_sec;
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);

    const bool colon_on = ((tick_ / 30) % 2) == 0;
    if (tm_info.tm_hour != last_hour_ || tm_info.tm_min != last_min_ || colon_on != last_colon_) {
        PaintDigits(&tm_info, colon_on);
        last_hour_ = tm_info.tm_hour;
        last_min_ = tm_info.tm_min;
        last_colon_ = colon_on;
    }

    AnimateAmbient();
    AnimateEyes();
    AnimateBars();
}

void ValeraLcdDisplay::AnimateAmbient() {
    const float breath = 0.5f + 0.5f * sinf(tick_ * 0.025f);
    lv_color_t rim = kCyanDim;
    if (mode_ == FaceMode::kListen) rim = kCyan;
    else if (mode_ == FaceMode::kSpeak) rim = kMagenta;
    else if (mode_ == FaceMode::kThink) rim = kThink;
    else if (mode_ == FaceMode::kWifi) rim = ((tick_ / 28) % 2) ? kCyan : kMagenta;

    lv_obj_set_style_border_color(bezel_, rim, 0);
    lv_obj_set_style_border_opa(bezel_, (lv_opa_t)(40 + (int)(55 * breath)), 0);
}

void ValeraLcdDisplay::AnimateEyes() {
    if (mode_ == FaceMode::kSleep) {
        PlacePupil(&left_, 0.f, 0.f, 0.12f);
        PlacePupil(&right_, 0.f, 0.f, 0.12f);
        return;
    }

    if (tick_ >= next_gaze_tick_) PickGazeTarget();

    float bias_x = 0.f;
    float bias_y = 0.f;
    float speed = 0.09f;
    float want_squint = 0.f;
    lv_color_t eye_color = kEye;

    switch (mode_) {
        case FaceMode::kListen:
            speed = 0.14f;
            eye_color = lv_color_hex(0xE8FCFF);
            break;
        case FaceMode::kSpeak:
            speed = 0.12f;
            want_squint = 0.18f;  // happy squint like RoboEyes / Sber warm
            eye_color = lv_color_hex(0xFFE8F6);
            break;
        case FaceMode::kThink:
            speed = 0.06f;
            bias_x = ((tick_ / 100) % 2 == 0) ? -eye_w_ * 0.14f : eye_w_ * 0.14f;
            bias_y = -eye_h_ * 0.06f;
            want_squint = 0.10f;
            eye_color = lv_color_hex(0xEEF0FF);
            break;
        case FaceMode::kWifi:
            eye_color = ((tick_ / 28) % 2) ? lv_color_hex(0xE8FCFF) : lv_color_hex(0xFFE8F6);
            break;
        default:
            break;
    }

    look_x_ += ((target_x_ + bias_x) - look_x_) * speed;
    look_y_ += ((target_y_ + bias_y) - look_y_) * speed;
    squint_ += (want_squint - squint_) * 0.12f;

    if (blink_phase_ == 0) {
        if (tick_ >= next_blink_tick_) {
            blink_phase_ = 1;
            blink_tick_ = 0;
        }
    } else if (blink_phase_ == 1) {
        blink_tick_++;
        blink_ = Smoothstep(blink_tick_ / 4.f);
        if (blink_ >= 1.f) {
            blink_ = 1.f;
            blink_phase_ = 2;
            blink_tick_ = 0;
        }
    } else {
        blink_tick_++;
        blink_ = 1.f - Smoothstep(blink_tick_ / 4.f);
        if (blink_ <= 0.f) {
            blink_ = 0.f;
            blink_phase_ = 0;
            next_blink_tick_ = tick_ + 110 + (rand() % 160);
        }
    }

    const float close = Clampf(blink_ > squint_ ? blink_ : squint_, 0.f, 1.f);
    const float open = 1.f - close * 0.92f;

    lv_obj_set_style_bg_color(left_.body, eye_color, 0);
    lv_obj_set_style_bg_color(right_.body, eye_color, 0);
    PlacePupil(&left_, look_x_, look_y_, open);
    PlacePupil(&right_, look_x_, look_y_, open);
}

void ValeraLcdDisplay::AnimateBars() {
    const bool show = mode_ == FaceMode::kSpeak || mode_ == FaceMode::kListen;
    const int base_y = eye_cy_ + eye_h_ / 2 + 22;
    for (int i = 0; i < 6; ++i) {
        if (!show) {
            if (!lv_obj_has_flag(bars_[i], LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }
        lv_obj_clear_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
        float phase = tick_ * 0.18f + i * 0.6f;
        float env = mode_ == FaceMode::kListen ? 0.4f + 0.6f * fabsf(sinf(phase))
                                               : 0.45f + 0.55f * sinf(phase);
        int h = 8 + (int)(28.f * env);
        lv_obj_set_height(bars_[i], h);
        lv_obj_set_y(bars_[i], base_y + 28 - h);
        lv_obj_set_style_bg_color(bars_[i], (i % 2) ? kMagenta : kCyan, 0);
        lv_obj_set_style_bg_opa(bars_[i], (lv_opa_t)(180 + (int)(60 * env)), 0);
    }
}

void ValeraLcdDisplay::PaintDigits(const struct tm* tm, bool colon_on) {
    if (tm == nullptr) return;
    int digits[4] = {tm->tm_hour / 10, tm->tm_hour % 10, tm->tm_min / 10, tm->tm_min % 10};
    for (int d = 0; d < 4; ++d) {
        uint8_t bits = kSegBits[digits[d] % 10];
        for (int s = 0; s < 7; ++s) {
            const bool lit = (bits & (1 << s)) != 0;
            // Fully off segments disappear on AMOLED (true black) for cleaner digits.
            lv_obj_set_style_bg_color(seg_[d][s], lit ? kCyan : kBg, 0);
            lv_obj_set_style_bg_opa(seg_[d][s], lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
    }
    const lv_opa_t colon_opa = colon_on ? LV_OPA_COVER : LV_OPA_30;
    lv_obj_set_style_bg_opa(colon_[0], colon_opa, 0);
    lv_obj_set_style_bg_opa(colon_[1], colon_opa, 0);
}

void ValeraLcdDisplay::ApplyMode(FaceMode mode) {
    mode_ = mode;
    PickGazeTarget();
}

void ValeraLcdDisplay::SetStatus(const char* status) {
    if (status == nullptr) return;
    LvglDisplay::SetStatus(status);
}

void ValeraLcdDisplay::ShowNotification(const char* notification, int duration_ms) {
    LvglDisplay::ShowNotification(notification, duration_ms);
}

void ValeraLcdDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void ValeraLcdDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) return;
    if (std::strcmp(emotion, "thinking") == 0) {
        ApplyMode(FaceMode::kThink);
    } else if (std::strcmp(emotion, "happy") == 0 || std::strcmp(emotion, "laughing") == 0) {
        ApplyMode(FaceMode::kSpeak);
    } else if (std::strcmp(emotion, "sleepy") == 0) {
        ApplyMode(FaceMode::kSleep);
    }
}

void ValeraLcdDisplay::SetChatMessage(const char* role, const char* content) {
    (void)role;
    (void)content;
}

void ValeraLcdDisplay::ClearChatMessages() {}

void ValeraLcdDisplay::UpdateStatusBar(bool update_all) {
    (void)update_all;
}

void ValeraLcdDisplay::SetPowerSaveMode(bool on) {
    sleepy_ = on;
    if (on) {
        ApplyMode(FaceMode::kSleep);
    } else if (face_ready_) {
        ApplyMode(FaceMode::kIdle);
    }
}

void ValeraLcdDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);
}
