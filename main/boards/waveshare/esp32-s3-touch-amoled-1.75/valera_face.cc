#include "valera_face.h"

#include "application.h"
#include "device_state.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "valera_sensors.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#define TAG "ValeraFace"

namespace {

const lv_color_t kBg = lv_color_hex(0x000000);
const lv_color_t kCyan = lv_color_hex(0x00E8FF);
const lv_color_t kCyanDim = lv_color_hex(0x003844);
const lv_color_t kMagenta = lv_color_hex(0xFF2BD6);
const lv_color_t kMagentaDim = lv_color_hex(0x3A0A30);
const lv_color_t kSegOff = lv_color_hex(0x0B0F14);
const lv_color_t kWhite = lv_color_hex(0xF5F7FA);
const lv_color_t kSclera = lv_color_hex(0x0C1218);
const lv_color_t kRing = lv_color_hex(0x142028);
const lv_color_t kCaption = lv_color_hex(0xD7DEE7);

// 7-segment bit masks for digits 0-9 (a b c d e f g).
constexpr uint8_t kSegBits[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

const char* kWeekdaysRu[] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
const char* kMonthsRu[] = {"янв", "фев", "мар", "апр", "май", "июн",
                           "июл", "авг", "сен", "окт", "ноя", "дек"};

lv_obj_t* MakePlain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
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

float Smoothstep(float t) {
    if (t < 0.f) return 0.f;
    if (t > 1.f) return 1.f;
    return t * t * (3.f - 2.f * t);
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

void ValeraLcdDisplay::BuildFace() {
    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, kBg, 0);

    root_ = MakePlain(screen);
    lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(root_, kBg, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(root_, 0, 0);

    // Thin circular frame for the round AMOLED bezel.
    bezel_ring_ = MakePlain(root_);
    lv_obj_set_size(bezel_ring_, LV_HOR_RES - 18, LV_VER_RES - 18);
    lv_obj_center(bezel_ring_);
    lv_obj_set_style_radius(bezel_ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(bezel_ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bezel_ring_, 2, 0);
    lv_obj_set_style_border_color(bezel_ring_, kCyanDim, 0);
    lv_obj_set_style_border_opa(bezel_ring_, LV_OPA_60, 0);

    auto theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* font = theme && theme->text_font() ? theme->text_font()->font() : nullptr;

    brand_ = lv_label_create(root_);
    if (font) lv_obj_set_style_text_font(brand_, font, 0);
    lv_obj_set_style_text_color(brand_, kMagenta, 0);
    lv_obj_set_style_text_letter_space(brand_, 2, 0);
    lv_label_set_text(brand_, "ВАЛЕРА");
    lv_obj_align(brand_, LV_ALIGN_TOP_MID, 0, 28);

    date_label_ = lv_label_create(root_);
    if (font) lv_obj_set_style_text_font(date_label_, font, 0);
    lv_obj_set_style_text_color(date_label_, kCyan, 0);
    lv_obj_set_style_text_opa(date_label_, LV_OPA_80, 0);
    lv_obj_align(date_label_, LV_ALIGN_TOP_MID, 0, 54);
    lv_label_set_text(date_label_, "");

    // Digits: four 7-segment blocks, centered, larger for hero presence.
    const int digit_w = 78;
    const int digit_h = 132;
    const int gap = 10;
    const int colon_w = 18;
    const int total_w = digit_w * 4 + gap * 3 + colon_w;
    const int origin_x = (LV_HOR_RES - total_w) / 2;
    const int origin_y = 84;
    const int thick = 12;
    const int inset = 8;

    int xs[4];
    xs[0] = origin_x;
    xs[1] = origin_x + digit_w + gap;
    xs[2] = xs[1] + digit_w + gap + colon_w;
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

    const int colon_x = xs[1] + digit_w + (gap + colon_w - 12) / 2;
    colon_[0] = MakeSeg(root_, colon_x, origin_y + 36, 12, 12);
    colon_[1] = MakeSeg(root_, colon_x, origin_y + 84, 12, 12);
    lv_obj_set_style_bg_color(colon_[0], kCyan, 0);
    lv_obj_set_style_bg_color(colon_[1], kCyan, 0);
    lv_obj_set_style_radius(colon_[0], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(colon_[1], LV_RADIUS_CIRCLE, 0);

    // Soft halos + expressive eyes under the clock.
    for (int i = 0; i < 2; ++i) {
        const int cx = LV_HOR_RES / 2 - 92 + i * 112;
        const int cy = 246;

        halo_[i] = MakePlain(root_);
        lv_obj_set_size(halo_[i], 96, 96);
        lv_obj_set_pos(halo_[i], cx - 16, cy - 16);
        lv_obj_set_style_radius(halo_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(halo_[i], i ? kMagenta : kCyan, 0);
        lv_obj_set_style_bg_opa(halo_[i], LV_OPA_20, 0);

        eye_socket_[i] = MakePlain(root_);
        lv_obj_set_size(eye_socket_[i], 72, 72);
        lv_obj_set_pos(eye_socket_[i], cx - 4, cy - 4);
        lv_obj_set_style_radius(eye_socket_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(eye_socket_[i], kRing, 0);
        lv_obj_set_style_bg_opa(eye_socket_[i], LV_OPA_COVER, 0);

        eye_[i] = MakePlain(eye_socket_[i]);
        lv_obj_set_size(eye_[i], 64, 64);
        lv_obj_center(eye_[i]);
        lv_obj_set_style_radius(eye_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(eye_[i], kSclera, 0);
        lv_obj_set_style_bg_opa(eye_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(eye_[i], 2, 0);
        lv_obj_set_style_border_color(eye_[i], kCyan, 0);
        lv_obj_set_style_clip_corner(eye_[i], true, 0);

        iris_[i] = MakePlain(eye_[i]);
        lv_obj_set_size(iris_[i], 30, 30);
        lv_obj_center(iris_[i]);
        lv_obj_set_style_radius(iris_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(iris_[i], kMagenta, 0);
        lv_obj_set_style_border_width(iris_[i], 2, 0);
        lv_obj_set_style_border_color(iris_[i], kCyan, 0);

        pupil_[i] = MakePlain(iris_[i]);
        lv_obj_set_size(pupil_[i], 12, 12);
        lv_obj_center(pupil_[i]);
        lv_obj_set_style_radius(pupil_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(pupil_[i], kBg, 0);

        shine_[i] = MakePlain(iris_[i]);
        lv_obj_set_size(shine_[i], 6, 6);
        lv_obj_align(shine_[i], LV_ALIGN_TOP_LEFT, 4, 4);
        lv_obj_set_style_radius(shine_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(shine_[i], kWhite, 0);
        lv_obj_set_style_bg_opa(shine_[i], LV_OPA_80, 0);

        lid_[i] = MakePlain(eye_[i]);
        lv_obj_set_size(lid_[i], 64, 0);
        lv_obj_align(lid_[i], LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(lid_[i], kBg, 0);
        lv_obj_set_style_bg_opa(lid_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(lid_[i], 0, 0);
    }

    mode_chip_ = MakePlain(root_);
    lv_obj_set_size(mode_chip_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(mode_chip_, 16, 0);
    lv_obj_set_style_bg_color(mode_chip_, lv_color_hex(0x081018), 0);
    lv_obj_set_style_bg_opa(mode_chip_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(mode_chip_, 14, 0);
    lv_obj_set_style_pad_ver(mode_chip_, 4, 0);
    lv_obj_set_style_border_width(mode_chip_, 1, 0);
    lv_obj_set_style_border_color(mode_chip_, kCyanDim, 0);
    lv_obj_align(mode_chip_, LV_ALIGN_CENTER, 0, 78);
    lv_obj_t* mode_lbl = lv_label_create(mode_chip_);
    if (font) lv_obj_set_style_text_font(mode_lbl, font, 0);
    lv_obj_set_style_text_color(mode_lbl, kCyan, 0);
    lv_label_set_text(mode_lbl, "рядом");
    lv_obj_center(mode_lbl);
    // stash label as user data via child index 0 — already the only child

    // Climate pills with leading color dots.
    auto make_pill = [&](lv_obj_t** pill, lv_obj_t** dot, lv_obj_t** label, lv_color_t accent,
                         lv_color_t bg, int x_off) {
        *pill = MakePlain(root_);
        lv_obj_set_size(*pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(*pill, 20, 0);
        lv_obj_set_style_bg_color(*pill, bg, 0);
        lv_obj_set_style_bg_opa(*pill, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(*pill, 12, 0);
        lv_obj_set_style_pad_ver(*pill, 8, 0);
        lv_obj_set_style_border_width(*pill, 1, 0);
        lv_obj_set_style_border_color(*pill, accent, 0);
        lv_obj_set_style_border_opa(*pill, LV_OPA_40, 0);
        lv_obj_set_flex_flow(*pill, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(*pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(*pill, 8, 0);
        lv_obj_align(*pill, LV_ALIGN_BOTTOM_MID, x_off, -70);

        *dot = MakePlain(*pill);
        lv_obj_set_size(*dot, 8, 8);
        lv_obj_set_style_radius(*dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(*dot, accent, 0);
        lv_obj_set_style_bg_opa(*dot, LV_OPA_COVER, 0);

        *label = lv_label_create(*pill);
        if (font) lv_obj_set_style_text_font(*label, font, 0);
        lv_obj_set_style_text_color(*label, accent, 0);
    };

    make_pill(&out_pill_, &out_dot_, &out_label_, kCyan, lv_color_hex(0x071820), -102);
    lv_label_set_text(out_label_, "улица …");
    make_pill(&room_pill_, &room_dot_, &room_label_, kMagenta, lv_color_hex(0x1A0818), 102);
    lv_label_set_text(room_label_, "комната …");

    caption_ = lv_label_create(root_);
    if (font) lv_obj_set_style_text_font(caption_, font, 0);
    lv_obj_set_style_text_color(caption_, kCaption, 0);
    lv_obj_set_width(caption_, LV_HOR_RES - 72);
    lv_obj_set_style_text_align(caption_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(caption_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(caption_, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_label_set_text(caption_, "");
    lv_obj_add_flag(caption_, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 9; ++i) {
        bars_[i] = MakePlain(root_);
        lv_obj_set_size(bars_[i], 8, 14);
        lv_obj_set_pos(bars_[i], LV_HOR_RES / 2 - 68 + i * 16, 338);
        lv_obj_set_style_radius(bars_[i], 4, 0);
        lv_obj_set_style_bg_color(bars_[i], kCyan, 0);
        lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
    }

    ApplyMode(FaceMode::kIdle);
}

void ValeraLcdDisplay::TickThunk(lv_timer_t* timer) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->Tick();
}

void ValeraLcdDisplay::SetModeChip(const char* text, lv_color_t color) {
    if (mode_chip_ == nullptr) return;
    lv_obj_t* lbl = lv_obj_get_child(mode_chip_, 0);
    if (lbl) {
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_color(lbl, color, 0);
    }
    lv_obj_set_style_border_color(mode_chip_, color, 0);
}

void ValeraLcdDisplay::Tick() {
    if (!face_ready_ || sleepy_) return;
    ++tick_;

    FaceMode want = FaceMode::kIdle;
    auto state = Application::GetInstance().GetDeviceState();
    switch (state) {
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

    // Soft colon pulse (~1 Hz) instead of hard blink.
    bool colon_on = ((tick_ / 30) % 2) == 0;
    if (tm_info.tm_hour != last_hour_ || tm_info.tm_min != last_min_ || colon_on != last_colon_) {
        PaintDigits(&tm_info, colon_on);
        last_hour_ = tm_info.tm_hour;
        last_min_ = tm_info.tm_min;
        last_colon_ = colon_on;
    }

    if ((tick_ % 60) == 0) PaintInfo();

    AnimateAmbient();
    AnimateEyes();
    AnimateBars();

    if (caption_dirty_) {
        caption_dirty_ = false;
        if (caption_text_[0] == '\0') {
            lv_obj_add_flag(caption_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(caption_, caption_text_);
            lv_obj_clear_flag(caption_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ValeraLcdDisplay::AnimateAmbient() {
    // Slow breathing on bezel + halos — cheap opacity/color only.
    const float breath = 0.5f + 0.5f * sinf(tick_ * 0.035f);
    const lv_opa_t bezel_opa = (lv_opa_t)(40 + (int)(50 * breath));
    lv_obj_set_style_border_opa(bezel_ring_, bezel_opa, 0);
    if (mode_ == FaceMode::kIdle || mode_ == FaceMode::kWifi) {
        lv_obj_set_style_border_color(bezel_ring_, kCyanDim, 0);
    } else if (mode_ == FaceMode::kListen) {
        lv_obj_set_style_border_color(bezel_ring_, kCyan, 0);
    } else if (mode_ == FaceMode::kSpeak) {
        lv_obj_set_style_border_color(bezel_ring_, kMagenta, 0);
    } else {
        lv_obj_set_style_border_color(bezel_ring_, lv_color_hex(0x303060), 0);
    }

    const lv_opa_t halo_opa = (lv_opa_t)(18 + (int)(22 * breath));
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_style_bg_opa(halo_[i], halo_opa, 0);
    }
}

void ValeraLcdDisplay::AnimateEyes() {
    int look_x = 0;
    int look_y = 0;
    int iris = 30;
    int eye_h = 64;
    lv_color_t iris_color = kMagenta;
    lv_color_t ring = kCyan;

    // Blink envelope every ~2.4 s, ~120 ms close/open with smoothstep.
    const int blink_period = 144;  // ~2.3 s at 60 fps
    const int blink_phase = tick_ % blink_period;
    float lid = 0.f;
    if (mode_ != FaceMode::kSleep && blink_phase < 10) {
        lid = Smoothstep(blink_phase / 5.f);
        if (blink_phase >= 5) lid = Smoothstep((10 - blink_phase) / 5.f);
    }

    switch (mode_) {
        case FaceMode::kIdle:
            look_x = (int)(14.0f * sinf(tick_ * 0.033f));
            look_y = (int)(7.0f * sinf(tick_ * 0.021f + 1.2f));
            break;
        case FaceMode::kListen:
            look_x = (int)(11.0f * sinf(tick_ * 0.11f));
            look_y = 3 + (int)(3.0f * sinf(tick_ * 0.07f));
            iris_color = kCyan;
            ring = kMagenta;
            iris = 32;
            break;
        case FaceMode::kSpeak:
            look_x = (int)(6.0f * sinf(tick_ * 0.09f));
            look_y = (int)(9.0f * sinf(tick_ * 0.18f));
            iris = 34;
            ring = kMagenta;
            break;
        case FaceMode::kThink: {
            // Slow glance left-right with pause.
            const int cycle = tick_ % 120;
            look_x = cycle < 50 ? -12 : (cycle < 60 ? -12 + (cycle - 50) : (cycle < 110 ? 12 : 12));
            if (cycle >= 50 && cycle < 60) look_x = -12 + (cycle - 50) * 24 / 10;
            if (cycle >= 110) look_x = 12 - (cycle - 110) * 24 / 10;
            look_y = -5;
            iris_color = lv_color_hex(0x8FA6FF);
            ring = lv_color_hex(0x6070A0);
            break;
        }
        case FaceMode::kWifi:
            look_y = (int)(5.0f * sinf(tick_ * 0.13f));
            ring = kMagenta;
            iris_color = kCyan;
            break;
        case FaceMode::kSleep:
            eye_h = 6;
            lid = 1.f;
            look_x = 0;
            look_y = 0;
            break;
    }

    if (lid > 0.f && mode_ != FaceMode::kSleep) {
        eye_h = 64 - (int)(56.f * lid);
        if (eye_h < 8) eye_h = 8;
    }

    for (int i = 0; i < 2; ++i) {
        lv_obj_set_style_border_color(eye_[i], ring, 0);
        lv_obj_set_style_bg_color(halo_[i], i ? kMagenta : ring, 0);
        if (eye_h != last_eye_h_ || mode_ == FaceMode::kSleep) {
            lv_obj_set_height(eye_[i], eye_h);
            lv_obj_set_height(lid_[i], (int)(64 * lid));
        }
        if (iris != last_iris_) {
            lv_obj_set_size(iris_[i], iris, iris);
        }
        lv_obj_set_style_bg_color(iris_[i], iris_color, 0);
        lv_obj_set_style_border_color(iris_[i], ring, 0);
        if (look_x != last_look_x_ || look_y != last_look_y_ || iris != last_iris_) {
            // Slight vergence: outer eye looks a bit farther.
            const int verd = (i == 0) ? -1 : 1;
            lv_obj_align(iris_[i], LV_ALIGN_CENTER, look_x + verd, look_y);
        }
    }
    last_look_x_ = look_x;
    last_look_y_ = look_y;
    last_iris_ = iris;
    last_eye_h_ = eye_h;
}

void ValeraLcdDisplay::AnimateBars() {
    const bool show =
        mode_ == FaceMode::kSpeak || mode_ == FaceMode::kThink || mode_ == FaceMode::kListen;
    for (int i = 0; i < 9; ++i) {
        if (!show) {
            if (!lv_obj_has_flag(bars_[i], LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }
        lv_obj_clear_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
        float phase = tick_ * 0.22f + i * 0.55f;
        float env = 0.55f + 0.45f * sinf(phase);
        if (mode_ == FaceMode::kThink) env = 0.35f + 0.25f * fabsf(sinf(phase * 0.5f));
        if (mode_ == FaceMode::kListen) env = 0.4f + 0.6f * fabsf(sinf(phase));
        int h = 10 + (int)(34.f * env);
        lv_obj_set_height(bars_[i], h);
        lv_obj_set_style_bg_color(bars_[i], (i % 2) ? kMagenta : kCyan, 0);
        lv_obj_set_y(bars_[i], 352 - h);
    }
}

void ValeraLcdDisplay::PaintDigits(const struct tm* tm, bool colon_on) {
    if (tm == nullptr) return;
    int digits[4] = {tm->tm_hour / 10, tm->tm_hour % 10, tm->tm_min / 10, tm->tm_min % 10};
    for (int d = 0; d < 4; ++d) {
        uint8_t bits = kSegBits[digits[d] % 10];
        // Hours cyan, minutes slightly magenta-tinted cyan for depth.
        const lv_color_t on = (d < 2) ? kCyan : lv_color_hex(0x66F0FF);
        for (int s = 0; s < 7; ++s) {
            const bool lit = (bits & (1 << s)) != 0;
            lv_obj_set_style_bg_color(seg_[d][s], lit ? on : kSegOff, 0);
            lv_obj_set_style_bg_opa(seg_[d][s], lit ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }
    const lv_opa_t colon_opa = colon_on ? LV_OPA_COVER : LV_OPA_30;
    lv_obj_set_style_bg_opa(colon_[0], colon_opa, 0);
    lv_obj_set_style_bg_opa(colon_[1], colon_opa, 0);
}

void ValeraLcdDisplay::PaintInfo() {
    time_t now = time(nullptr);
    ValeraClimate c = ValeraSensors::Get();
    if (c.clock_fix_ok) now += c.clock_fix_sec;
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);

    int wday = tm_info.tm_wday;
    if (wday < 0 || wday > 6) wday = 0;
    int mon = tm_info.tm_mon;
    if (mon < 0 || mon > 11) mon = 0;
    char date_buf[48];
    std::snprintf(date_buf, sizeof(date_buf), "%s · %d %s", kWeekdaysRu[wday], tm_info.tm_mday,
                  kMonthsRu[mon]);
    lv_label_set_text(date_label_, date_buf);

    char out_buf[48];
    if (c.outdoor_ok) {
        std::snprintf(out_buf, sizeof(out_buf), "улица %d° %s", (int)lroundf(c.outdoor_c),
                      c.condition);
    } else {
        std::snprintf(out_buf, sizeof(out_buf), "улица …");
    }
    lv_label_set_text(out_label_, out_buf);

    char room_buf[48];
    if (c.room_ok) {
        std::snprintf(room_buf, sizeof(room_buf), "комната %d° %d%%", (int)lroundf(c.room_c),
                      (int)lroundf(c.room_rh));
    } else {
        std::snprintf(room_buf, sizeof(room_buf), "комната —");
    }
    lv_label_set_text(room_label_, room_buf);
}

void ValeraLcdDisplay::ApplyMode(FaceMode mode) {
    mode_ = mode;
    last_eye_h_ = -1;
    last_look_x_ = 999;
    last_iris_ = -1;
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_height(eye_[i], 64);
        lv_obj_set_height(lid_[i], 0);
    }
    switch (mode) {
        case FaceMode::kIdle:
            SetModeChip("рядом", kCyan);
            break;
        case FaceMode::kListen:
            SetModeChip("слушаю", kCyan);
            break;
        case FaceMode::kSpeak:
            SetModeChip("говорю", kMagenta);
            break;
        case FaceMode::kThink:
            SetModeChip("думаю", lv_color_hex(0x8FA6FF));
            break;
        case FaceMode::kWifi:
            SetModeChip("сеть", kMagenta);
            break;
        case FaceMode::kSleep:
            SetModeChip("сплю", kCyanDim);
            break;
    }
}

void ValeraLcdDisplay::SetStatus(const char* status) {
    if (status == nullptr) return;
    LvglDisplay::SetStatus(status);
}

void ValeraLcdDisplay::ShowNotification(const char* notification, int duration_ms) {
    if (notification) {
        std::snprintf(caption_text_, sizeof(caption_text_), "%s", notification);
        caption_dirty_ = true;
    }
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
    if (content == nullptr || content[0] == '\0') {
        caption_text_[0] = '\0';
    } else {
        std::snprintf(caption_text_, sizeof(caption_text_), "%s", content);
    }
    caption_dirty_ = true;
}

void ValeraLcdDisplay::ClearChatMessages() {
    caption_text_[0] = '\0';
    caption_dirty_ = true;
}

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
