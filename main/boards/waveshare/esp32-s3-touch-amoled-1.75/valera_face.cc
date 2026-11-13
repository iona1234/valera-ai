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

const lv_color_t kBg = lv_color_hex(0x000000);
const lv_color_t kCyan = lv_color_hex(0x00E5FF);
const lv_color_t kCyanDeep = lv_color_hex(0x007A8C);
const lv_color_t kMagenta = lv_color_hex(0xFF3D9A);
const lv_color_t kMagentaDeep = lv_color_hex(0x8C1A55);
const lv_color_t kThink = lv_color_hex(0x7B8CFF);
const lv_color_t kSclera = lv_color_hex(0xEAF2FF);
const lv_color_t kScleraEdge = lv_color_hex(0x1A2433);
const lv_color_t kIrisOuter = lv_color_hex(0x00B8D4);
const lv_color_t kIrisInner = lv_color_hex(0x5CFFF0);
const lv_color_t kPupil = lv_color_hex(0x05060A);
const lv_color_t kSegOff = lv_color_hex(0x0A1016);
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

lv_obj_t* MakeCircle(lv_obj_t* parent, int size, lv_color_t color, lv_opa_t opa) {
    lv_obj_t* o = MakePlain(parent);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
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
    const int eye_w = eye_rx_ * 2;
    const int eye_h = eye_ry_ * 2;

    eye->halo = MakeCircle(root_, eye_w + 36, kCyan, LV_OPA_10);
    lv_obj_set_pos(eye->halo, cx - eye_rx_ - 18, cy - eye_ry_ - 18);

    eye->socket = MakePlain(root_);
    lv_obj_set_size(eye->socket, eye_w + 8, eye_h + 8);
    lv_obj_set_pos(eye->socket, cx - eye_rx_ - 4, cy - eye_ry_ - 4);
    lv_obj_set_style_radius(eye->socket, (eye_h + 8) / 2, 0);
    lv_obj_set_style_bg_color(eye->socket, kScleraEdge, 0);
    lv_obj_set_style_bg_opa(eye->socket, LV_OPA_COVER, 0);

    eye->sclera = MakePlain(root_);
    lv_obj_set_size(eye->sclera, eye_w, eye_h);
    lv_obj_set_pos(eye->sclera, cx - eye_rx_, cy - eye_ry_);
    lv_obj_set_style_radius(eye->sclera, eye_ry_, 0);
    lv_obj_set_style_bg_color(eye->sclera, kSclera, 0);
    lv_obj_set_style_bg_opa(eye->sclera, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(eye->sclera, true, 0);
    lv_obj_set_style_border_width(eye->sclera, 3, 0);
    lv_obj_set_style_border_color(eye->sclera, kCyanDeep, 0);

    eye->iris_outer = MakeCircle(eye->sclera, iris_r_ * 2, kIrisOuter, LV_OPA_COVER);
    eye->iris_inner = MakeCircle(eye->sclera, (int)(iris_r_ * 1.35f), kIrisInner, (lv_opa_t)230);
    eye->pupil = MakeCircle(eye->sclera, pupil_r_ * 2, kPupil, LV_OPA_COVER);
    eye->shine_big = MakeCircle(eye->sclera, (int)(eye_rx_ * 0.28f), kWhite, (lv_opa_t)245);
    eye->shine_small = MakeCircle(eye->sclera, (int)(eye_rx_ * 0.12f), kWhite, (lv_opa_t)170);

    eye->lid_top = MakePlain(root_);
    lv_obj_set_size(eye->lid_top, eye_w, eye_ry_);
    lv_obj_set_style_bg_color(eye->lid_top, kBg, 0);
    lv_obj_set_style_bg_opa(eye->lid_top, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(eye->lid_top, eye_ry_, 0);

    eye->lid_bot = MakePlain(root_);
    lv_obj_set_size(eye->lid_bot, eye_w, eye_ry_);
    lv_obj_set_style_bg_color(eye->lid_bot, kBg, 0);
    lv_obj_set_style_bg_opa(eye->lid_bot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(eye->lid_bot, eye_ry_, 0);

    PlaceIris(eye, 0.f, 0.f);
    PlaceLids(eye, 0.f);
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

    // Soft outer wash — opacity only, stays centered.
    glow_ = MakePlain(root_);
    lv_obj_set_size(glow_, min_side - 48, min_side - 48);
    lv_obj_center(glow_);
    lv_obj_set_style_radius(glow_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(glow_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(glow_, 36, 0);
    lv_obj_set_style_border_color(glow_, kCyanDeep, 0);
    lv_obj_set_style_border_opa(glow_, LV_OPA_20, 0);

    bezel_ = MakePlain(root_);
    lv_obj_set_size(bezel_, min_side - 16, min_side - 16);
    lv_obj_center(bezel_);
    lv_obj_set_style_radius(bezel_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(bezel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bezel_, 2, 0);
    lv_obj_set_style_border_color(bezel_, kCyanDeep, 0);
    lv_obj_set_style_border_opa(bezel_, LV_OPA_60, 0);

    // Geometry scaled to the round panel — eyes are the hero under the clock.
    eye_rx_ = (int16_t)(min_side * 0.178f);
    if (eye_rx_ < 72) eye_rx_ = 72;
    eye_ry_ = (int16_t)(eye_rx_ * 0.78f);  // slightly wider capsule
    iris_r_ = (int16_t)(eye_rx_ * 0.55f);
    pupil_r_ = (int16_t)(eye_rx_ * 0.27f);

    // Clock in the upper third — large, centered, no labels.
    const int digit_w = (int)(min_side * 0.138f);
    const int digit_h = (int)(min_side * 0.220f);
    const int thick = (int)(digit_w * 0.16f);
    const int gap = (int)(digit_w * 0.12f);
    const int colon_gap = (int)(digit_w * 0.28f);
    const int total_w = digit_w * 4 + gap * 3 + colon_gap;
    const int origin_x = (W - total_w) / 2;
    const int origin_y = (int)(min_side * 0.12f);
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

    // Eyes centered under the clock, symmetric about vertical axis.
    const int16_t gap_eyes = (int16_t)(eye_rx_ * 0.36f);
    eye_cy_ = (int16_t)(origin_y + digit_h + eye_ry_ + min_side * 0.045f);
    const int16_t left_cx = (int16_t)(cx - eye_rx_ - gap_eyes);
    const int16_t right_cx = (int16_t)(cx + eye_rx_ + gap_eyes);
    BuildEye(&left_, left_cx, eye_cy_);
    BuildEye(&right_, right_cx, eye_cy_);

    // Soft mouth arc (hidden until speak) — geometric, no text.
    mouth_ = MakePlain(root_);
    lv_obj_set_size(mouth_, (int)(eye_rx_ * 1.6f), (int)(eye_ry_ * 0.45f));
    lv_obj_align(mouth_, LV_ALIGN_TOP_MID, 0, eye_cy_ + eye_ry_ + 10);
    lv_obj_set_style_radius(mouth_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(mouth_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mouth_, 4, 0);
    lv_obj_set_style_border_side(mouth_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(mouth_, kMagenta, 0);
    lv_obj_set_style_border_opa(mouth_, LV_OPA_COVER, 0);
    lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);

    // Wide centered EQ bars for listen/speak — still no text.
    for (int i = 0; i < 8; ++i) {
        bars_[i] = MakePlain(root_);
        lv_obj_set_size(bars_[i], 10, 16);
        lv_obj_set_pos(bars_[i], cx - 70 + i * 18, eye_cy_ + eye_ry_ + 28);
        lv_obj_set_style_radius(bars_[i], 5, 0);
        lv_obj_set_style_bg_color(bars_[i], kCyan, 0);
        lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
    }

    PickGazeTarget();
    next_blink_tick_ = 90 + (rand() % 120);
    ApplyMode(FaceMode::kIdle);
}

void ValeraLcdDisplay::TickThunk(lv_timer_t* timer) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->Tick();
}

void ValeraLcdDisplay::PickGazeTarget() {
    const float max_x = eye_rx_ * 0.30f;
    const float max_y = eye_ry_ * 0.26f;
    target_x_ = Frand(-max_x, max_x);
    target_y_ = Frand(-max_y, max_y);
    next_gaze_tick_ = tick_ + 50 + (rand() % 100);  // ~0.8–2.4 s
}

void ValeraLcdDisplay::PlaceIris(Eye* eye, float look_x, float look_y) {
    const int eye_w = eye_rx_ * 2;
    const int eye_h = eye_ry_ * 2;
    float ix = (float)eye_rx_ + look_x;
    float iy = (float)eye_ry_ + look_y;
    ix = Clampf(ix, (float)iris_r_, (float)(eye_w - iris_r_));
    iy = Clampf(iy, (float)iris_r_, (float)(eye_h - iris_r_));

    const int inner_r = (int)(iris_r_ * 0.67f);
    lv_obj_set_pos(eye->iris_outer, (int)ix - iris_r_, (int)iy - iris_r_);
    lv_obj_set_pos(eye->iris_inner, (int)ix - inner_r - (int)(iris_r_ * 0.12f),
                   (int)iy - inner_r - (int)(iris_r_ * 0.12f));
    lv_obj_set_pos(eye->pupil, (int)ix - pupil_r_, (int)iy - pupil_r_);

    const int shine1 = (int)(eye_rx_ * 0.14f);
    const int shine2 = (int)(eye_rx_ * 0.06f);
    lv_obj_set_pos(eye->shine_big, (int)ix - pupil_r_ - shine1, (int)iy - pupil_r_ - shine1);
    lv_obj_set_pos(eye->shine_small, (int)ix + shine2, (int)iy + shine2);
}

void ValeraLcdDisplay::PlaceLids(Eye* eye, float close_amount) {
    close_amount = Clampf(close_amount, 0.f, 1.f);
    const int eye_w = eye_rx_ * 2;
    const int lid_h = eye_ry_;
    const int top_open = eye->cy - eye_ry_ - lid_h + 2;
    const int top_closed = eye->cy - lid_h / 2;
    const int bot_open = eye->cy + eye_ry_ - 2;
    const int bot_closed = eye->cy - lid_h / 2;
    const int top_y = (int)(top_open + (top_closed - top_open) * close_amount);
    const int bot_y = (int)(bot_open + (bot_closed - bot_open) * close_amount);
    lv_obj_set_size(eye->lid_top, eye_w, lid_h);
    lv_obj_set_size(eye->lid_bot, eye_w, lid_h);
    lv_obj_set_pos(eye->lid_top, eye->cx - eye_rx_, top_y);
    lv_obj_set_pos(eye->lid_bot, eye->cx - eye_rx_, bot_y);
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
    const float breath = 0.5f + 0.5f * sinf(tick_ * 0.028f);
    lv_color_t rim = kCyanDeep;
    if (mode_ == FaceMode::kListen) rim = kCyan;
    else if (mode_ == FaceMode::kSpeak) rim = kMagenta;
    else if (mode_ == FaceMode::kThink) rim = kThink;
    else if (mode_ == FaceMode::kWifi) rim = ((tick_ / 24) % 2) ? kCyan : kMagenta;

    lv_obj_set_style_border_color(bezel_, rim, 0);
    lv_obj_set_style_border_opa(bezel_, (lv_opa_t)(50 + (int)(70 * breath)), 0);
    lv_obj_set_style_border_color(glow_, rim, 0);
    lv_obj_set_style_border_opa(glow_, (lv_opa_t)(12 + (int)(22 * breath)), 0);

    // Halo shimmer under eyes — opacity only, fixed position.
    const lv_opa_t halo = (lv_opa_t)(8 + (int)(18 * breath));
    lv_obj_set_style_bg_opa(left_.halo, halo, 0);
    lv_obj_set_style_bg_opa(right_.halo, halo, 0);
    lv_obj_set_style_bg_color(left_.halo, rim, 0);
    lv_obj_set_style_bg_color(right_.halo, rim, 0);
}

void ValeraLcdDisplay::AnimateEyes() {
    // Smooth saccades: lerp toward a target, then pick a new one.
    if (mode_ == FaceMode::kSleep) {
        look_x_ = 0.f;
        look_y_ = 0.f;
        blink_ = 1.f;
        PlaceIris(&left_, look_x_, look_y_);
        PlaceIris(&right_, look_x_, look_y_);
        PlaceLids(&left_, 1.f);
        PlaceLids(&right_, 1.f);
        return;
    }

    if (tick_ >= next_gaze_tick_) PickGazeTarget();

    // Mode-biased gaze.
    float bias_x = 0.f;
    float bias_y = 0.f;
    float gaze_speed = 0.10f;
    float want_squint = 0.f;
    lv_color_t iris_o = kIrisOuter;
    lv_color_t iris_i = kIrisInner;
    lv_color_t rim = kCyanDeep;

    switch (mode_) {
        case FaceMode::kListen:
            gaze_speed = 0.16f;
            iris_o = kCyan;
            iris_i = lv_color_hex(0xB8FFFF);
            rim = kMagentaDeep;
            break;
        case FaceMode::kSpeak:
            gaze_speed = 0.14f;
            bias_y = 2.f * sinf(tick_ * 0.12f);
            iris_o = kMagenta;
            iris_i = lv_color_hex(0xFFB0E0);
            rim = kMagenta;
            want_squint = 0.12f;
            break;
        case FaceMode::kThink:
            gaze_speed = 0.07f;
            // Hold left then right — still lerped, not teleported.
            if ((tick_ / 90) % 2 == 0) bias_x = -eye_rx_ * 0.22f;
            else bias_x = eye_rx_ * 0.22f;
            bias_y = -eye_ry_ * 0.10f;
            iris_o = kThink;
            iris_i = lv_color_hex(0xC8D0FF);
            rim = kThink;
            want_squint = 0.08f;
            break;
        case FaceMode::kWifi:
            iris_o = ((tick_ / 24) % 2) ? kCyan : kMagenta;
            iris_i = kWhite;
            rim = iris_o;
            break;
        case FaceMode::kIdle:
        default:
            break;
    }

    look_x_ += ((target_x_ + bias_x) - look_x_) * gaze_speed;
    look_y_ += ((target_y_ + bias_y) - look_y_) * gaze_speed;
    squint_ += (want_squint - squint_) * 0.12f;

    // Blink state machine (~80 ms close / open).
    if (blink_phase_ == 0) {
        if (tick_ >= next_blink_tick_) {
            blink_phase_ = 1;
            blink_tick_ = 0;
        }
    } else if (blink_phase_ == 1) {
        blink_tick_++;
        blink_ = Smoothstep(blink_tick_ / 5.f);
        if (blink_ >= 1.f) {
            blink_ = 1.f;
            blink_phase_ = 2;
            blink_tick_ = 0;
        }
    } else {
        blink_tick_++;
        blink_ = 1.f - Smoothstep(blink_tick_ / 5.f);
        if (blink_ <= 0.f) {
            blink_ = 0.f;
            blink_phase_ = 0;
            next_blink_tick_ = tick_ + 100 + (rand() % 140);
        }
    }

    const float close_amt = Clampf(blink_ > squint_ ? blink_ : squint_, 0.f, 1.f);

    for (Eye* eye : {&left_, &right_}) {
        lv_obj_set_style_bg_color(eye->iris_outer, iris_o, 0);
        lv_obj_set_style_bg_color(eye->iris_inner, iris_i, 0);
        lv_obj_set_style_border_color(eye->sclera, rim, 0);
        PlaceIris(eye, look_x_, look_y_);
        PlaceLids(eye, close_amt);
    }

    if (mode_ == FaceMode::kSpeak) {
        lv_obj_clear_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
        const float smile = 0.55f + 0.45f * fabsf(sinf(tick_ * 0.18f));
        lv_obj_set_style_border_opa(mouth_, (lv_opa_t)(120 + (int)(120 * smile)), 0);
    } else {
        if (!lv_obj_has_flag(mouth_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(mouth_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ValeraLcdDisplay::AnimateBars() {
    const bool show = mode_ == FaceMode::kSpeak || mode_ == FaceMode::kListen;
    const int base_y = eye_cy_ + eye_ry_ + 28;
    for (int i = 0; i < 8; ++i) {
        if (!show) {
            if (!lv_obj_has_flag(bars_[i], LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }
        lv_obj_clear_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
        float phase = tick_ * 0.20f + i * 0.55f;
        float env = mode_ == FaceMode::kListen ? 0.35f + 0.65f * fabsf(sinf(phase))
                                               : 0.45f + 0.55f * sinf(phase);
        int h = 10 + (int)(36.f * env);
        lv_obj_set_height(bars_[i], h);
        lv_obj_set_y(bars_[i], base_y + 36 - h);
        lv_obj_set_style_bg_color(bars_[i], (i % 2) ? kMagenta : kCyan, 0);
        lv_obj_set_style_bg_opa(bars_[i], (lv_opa_t)(170 + (int)(70 * env)), 0);
    }
}

void ValeraLcdDisplay::PaintDigits(const struct tm* tm, bool colon_on) {
    if (tm == nullptr) return;
    int digits[4] = {tm->tm_hour / 10, tm->tm_hour % 10, tm->tm_min / 10, tm->tm_min % 10};
    for (int d = 0; d < 4; ++d) {
        uint8_t bits = kSegBits[digits[d] % 10];
        for (int s = 0; s < 7; ++s) {
            const bool lit = (bits & (1 << s)) != 0;
            lv_obj_set_style_bg_color(seg_[d][s], lit ? kCyan : kSegOff, 0);
            lv_obj_set_style_bg_opa(seg_[d][s], lit ? LV_OPA_COVER : LV_OPA_30, 0);
        }
    }
    const lv_opa_t colon_opa = colon_on ? LV_OPA_COVER : LV_OPA_20;
    lv_obj_set_style_bg_opa(colon_[0], colon_opa, 0);
    lv_obj_set_style_bg_opa(colon_[1], colon_opa, 0);
}

void ValeraLcdDisplay::ApplyMode(FaceMode mode) {
    mode_ = mode;
    // Force a fresh gaze on mode change so eyes don't linger oddly.
    PickGazeTarget();
}

void ValeraLcdDisplay::SetStatus(const char* status) {
    if (status == nullptr) return;
    LvglDisplay::SetStatus(status);
}

void ValeraLcdDisplay::ShowNotification(const char* notification, int duration_ms) {
    // No tiny on-face captions — keep stock path for logging only.
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
    // Face stays visual-only: no unreadably small chat text on 1.75".
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
