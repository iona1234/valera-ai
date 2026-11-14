#include "valera_face.h"

#include "application.h"
#include "device_state.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "valera_sensors.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define TAG "ValeraFace"

namespace {

const lv_color_t kBg = lv_color_hex(0x000000);
const lv_color_t kCyan = lv_color_hex(0x00E8FF);
const lv_color_t kCyanDim = lv_color_hex(0x004A55);
const lv_color_t kMagenta = lv_color_hex(0xFF4DB8);
const lv_color_t kMagentaDim = lv_color_hex(0x5A1840);
const lv_color_t kThink = lv_color_hex(0x8FA6FF);
const lv_color_t kEye = lv_color_hex(0xF2F6FA);
const lv_color_t kPupil = lv_color_hex(0x0A0C10);
const lv_color_t kWhite = lv_color_hex(0xFFFFFF);
const lv_color_t kSun = lv_color_hex(0xFFD36A);
const lv_color_t kCloud = lv_color_hex(0xC8D6E5);

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
    lv_obj_set_style_bg_color(s, kBg, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
    return s;
}

void BuildDigit(lv_obj_t* parent, lv_obj_t* segs[7], int x, int y, int dw, int dh, int thick) {
    const int inset = thick / 2 + 1;
    segs[0] = MakeSeg(parent, x + inset, y, dw - 2 * inset, thick);
    segs[1] = MakeSeg(parent, x + dw - thick, y + inset, thick, dh / 2 - inset);
    segs[2] = MakeSeg(parent, x + dw - thick, y + dh / 2, thick, dh / 2 - inset);
    segs[3] = MakeSeg(parent, x + inset, y + dh - thick, dw - 2 * inset, thick);
    segs[4] = MakeSeg(parent, x, y + dh / 2, thick, dh / 2 - inset);
    segs[5] = MakeSeg(parent, x, y + inset, thick, dh / 2 - inset);
    segs[6] = MakeSeg(parent, x + inset, y + dh / 2 - thick / 2, dw - 2 * inset, thick);
}

void LitDigit(lv_obj_t* segs[7], int digit, lv_color_t on) {
    if (digit < 0 || digit > 9) {
        for (int s = 0; s < 7; ++s) lv_obj_set_style_bg_opa(segs[s], LV_OPA_TRANSP, 0);
        return;
    }
    uint8_t bits = kSegBits[digit];
    for (int s = 0; s < 7; ++s) {
        const bool lit = (bits & (1 << s)) != 0;
        lv_obj_set_style_bg_color(segs[s], lit ? on : kBg, 0);
        lv_obj_set_style_bg_opa(segs[s], lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
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

void Track(lv_obj_t** list, int* n, int cap, lv_obj_t* o) {
    if (o && *n < cap) list[(*n)++] = o;
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

void ValeraLcdDisplay::BuildEye(Eye* eye, int16_t cx, int16_t cy, int16_t w, int16_t h,
                                int16_t pupil_r) {
    eye->home_cx = cx;
    eye->home_cy = cy;
    eye->body = MakePlain(root_);
    lv_obj_set_size(eye->body, w, h);
    lv_obj_set_pos(eye->body, cx - w / 2, cy - h / 2);
    lv_obj_set_style_radius(eye->body, h / 2, 0);
    lv_obj_set_style_bg_color(eye->body, kEye, 0);
    lv_obj_set_style_bg_opa(eye->body, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(eye->body, true, 0);

    eye->pupil = MakePlain(eye->body);
    lv_obj_set_size(eye->pupil, pupil_r * 2, pupil_r * 2);
    lv_obj_set_style_radius(eye->pupil, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye->pupil, kPupil, 0);
    lv_obj_set_style_bg_opa(eye->pupil, LV_OPA_COVER, 0);

    eye->shine = MakePlain(eye->body);
    lv_obj_set_size(eye->shine, 8, 8);
    lv_obj_set_style_radius(eye->shine, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye->shine, kWhite, 0);
    lv_obj_set_style_bg_opa(eye->shine, LV_OPA_80, 0);

    PlacePupil(eye, w, h, pupil_r, 0.f, 0.f, 1.f);
}

void ValeraLcdDisplay::BuildFace() {
    const int W = LV_HOR_RES;
    const int H = LV_VER_RES;
    const int min_side = W < H ? W : H;
    const int cx = W / 2;
    const int cy = H / 2;

    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, kBg, 0);

    root_ = MakePlain(screen);
    lv_obj_set_size(root_, W, H);
    lv_obj_set_style_bg_color(root_, kBg, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    // Soft ambient rim only — not a heavy frame.
    bezel_ = MakePlain(root_);
    lv_obj_set_size(bezel_, min_side - 16, min_side - 16);
    lv_obj_center(bezel_);
    lv_obj_set_style_radius(bezel_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(bezel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bezel_, 2, 0);
    lv_obj_set_style_border_color(bezel_, kCyanDim, 0);
    lv_obj_set_style_border_opa(bezel_, LV_OPA_30, 0);

    // ── Clock ────────────────────────────────────────────────────────────
    const int c_dw = (int)(min_side * 0.155f);
    const int c_dh = (int)(min_side * 0.28f);
    const int c_th = (int)(c_dw * 0.22f);
    const int c_gap = (int)(c_dw * 0.16f);
    const int c_cgap = (int)(c_dw * 0.34f);
    const int c_total = c_dw * 4 + c_gap * 3 + c_cgap;
    const int c_ox = (W - c_total) / 2;
    const int c_oy = (H - c_dh) / 2;
    int cxs[4];
    cxs[0] = c_ox;
    cxs[1] = c_ox + c_dw + c_gap;
    cxs[2] = cxs[1] + c_dw + c_gap + c_cgap;
    cxs[3] = cxs[2] + c_dw + c_gap;
    for (int d = 0; d < 4; ++d) {
        BuildDigit(root_, clock_seg_[d], cxs[d], c_oy, c_dw, c_dh, c_th);
        for (int s = 0; s < 7; ++s) Track(clock_objs_, &clock_obj_n_, 30, clock_seg_[d][s]);
    }
    const int colon_x = cxs[1] + c_dw + (c_cgap - c_th) / 2;
    clock_colon_[0] = MakeSeg(root_, colon_x, c_oy + c_dh / 3 - c_th / 2, c_th, c_th);
    clock_colon_[1] = MakeSeg(root_, colon_x, c_oy + (2 * c_dh) / 3 - c_th / 2, c_th, c_th);
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_style_bg_color(clock_colon_[i], kCyan, 0);
        lv_obj_set_style_radius(clock_colon_[i], LV_RADIUS_CIRCLE, 0);
        Track(clock_objs_, &clock_obj_n_, 30, clock_colon_[i]);
    }

    // ── Weather ──────────────────────────────────────────────────────────
    weather_glyph_ = MakePlain(root_);
    lv_obj_set_size(weather_glyph_, 56, 56);
    lv_obj_set_pos(weather_glyph_, cx - 28, cy - 130);
    lv_obj_set_style_radius(weather_glyph_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(weather_glyph_, kSun, 0);
    lv_obj_set_style_bg_opa(weather_glyph_, LV_OPA_COVER, 0);
    Track(weather_objs_, &weather_obj_n_, 40, weather_glyph_);

    const int w_dw = (int)(min_side * 0.14f);
    const int w_dh = (int)(min_side * 0.24f);
    const int w_th = (int)(w_dw * 0.22f);
    const int w_gap = (int)(w_dw * 0.14f);
    const int out_total = w_th + 8 + w_dw * 2 + w_gap + 18;
    const int out_ox = (W - out_total) / 2;
    const int out_oy = cy - w_dh / 2 - 10;
    weather_out_minus_ = MakeSeg(root_, out_ox, out_oy + w_dh / 2 - w_th / 2, w_th + 4, w_th);
    Track(weather_objs_, &weather_obj_n_, 40, weather_out_minus_);
    BuildDigit(root_, weather_out_seg_[0], out_ox + w_th + 8, out_oy, w_dw, w_dh, w_th);
    BuildDigit(root_, weather_out_seg_[1], out_ox + w_th + 8 + w_dw + w_gap, out_oy, w_dw, w_dh,
               w_th);
    for (int d = 0; d < 2; ++d)
        for (int s = 0; s < 7; ++s) Track(weather_objs_, &weather_obj_n_, 40, weather_out_seg_[d][s]);
    weather_out_dot_ = MakePlain(root_);
    lv_obj_set_size(weather_out_dot_, 14, 14);
    lv_obj_set_pos(weather_out_dot_, out_ox + out_total - 16, out_oy + 8);
    lv_obj_set_style_radius(weather_out_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(weather_out_dot_, kCyan, 0);
    lv_obj_set_style_bg_opa(weather_out_dot_, LV_OPA_COVER, 0);
    Track(weather_objs_, &weather_obj_n_, 40, weather_out_dot_);

    const int r_dw = (int)(min_side * 0.11f);
    const int r_dh = (int)(min_side * 0.18f);
    const int r_th = (int)(r_dw * 0.22f);
    const int r_gap = (int)(r_dw * 0.14f);
    const int room_total = r_dw * 2 + r_gap + 16;
    const int room_ox = (W - room_total) / 2;
    const int room_oy = cy + 70;
    BuildDigit(root_, weather_room_seg_[0], room_ox, room_oy, r_dw, r_dh, r_th);
    BuildDigit(root_, weather_room_seg_[1], room_ox + r_dw + r_gap, room_oy, r_dw, r_dh, r_th);
    for (int d = 0; d < 2; ++d)
        for (int s = 0; s < 7; ++s)
            Track(weather_objs_, &weather_obj_n_, 40, weather_room_seg_[d][s]);
    weather_room_dot_ = MakePlain(root_);
    lv_obj_set_size(weather_room_dot_, 12, 12);
    lv_obj_set_pos(weather_room_dot_, room_ox + room_total - 14, room_oy + 6);
    lv_obj_set_style_radius(weather_room_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(weather_room_dot_, kMagenta, 0);
    lv_obj_set_style_bg_opa(weather_room_dot_, LV_OPA_COVER, 0);
    Track(weather_objs_, &weather_obj_n_, 40, weather_room_dot_);

    // ── Play / Sber screensaver: big centered eyes + dual rings + orbs ───
    play_eye_w_ = (int16_t)(min_side * 0.28f);
    play_eye_h_ = (int16_t)(play_eye_w_ * 0.62f);
    play_pupil_r_ = (int16_t)(play_eye_h_ * 0.30f);
    const int16_t play_gap = (int16_t)(play_eye_w_ * 0.28f);
    BuildEye(&play_left_, (int16_t)(cx - play_eye_w_ / 2 - play_gap), (int16_t)cy, play_eye_w_,
             play_eye_h_, play_pupil_r_);
    BuildEye(&play_right_, (int16_t)(cx + play_eye_w_ / 2 + play_gap), (int16_t)cy, play_eye_w_,
             play_eye_h_, play_pupil_r_);
    Track(play_objs_, &play_obj_n_, 28, play_left_.body);
    Track(play_objs_, &play_obj_n_, 28, play_right_.body);

    play_ring_ = MakePlain(root_);
    lv_obj_set_size(play_ring_, min_side - 70, min_side - 70);
    lv_obj_center(play_ring_);
    lv_obj_set_style_radius(play_ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(play_ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(play_ring_, 22, 0);
    lv_obj_set_style_border_color(play_ring_, kCyanDim, 0);
    lv_obj_set_style_border_opa(play_ring_, LV_OPA_20, 0);
    Track(play_objs_, &play_obj_n_, 28, play_ring_);

    play_ring2_ = MakePlain(root_);
    lv_obj_set_size(play_ring2_, min_side - 140, min_side - 140);
    lv_obj_center(play_ring2_);
    lv_obj_set_style_radius(play_ring2_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(play_ring2_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(play_ring2_, 10, 0);
    lv_obj_set_style_border_color(play_ring2_, kMagentaDim, 0);
    lv_obj_set_style_border_opa(play_ring2_, LV_OPA_20, 0);
    Track(play_objs_, &play_obj_n_, 28, play_ring2_);

    for (int i = 0; i < 10; ++i) {
        play_orb_[i] = MakePlain(root_);
        const int sz = (i % 3 == 0) ? 18 : ((i % 2) ? 10 : 14);
        lv_obj_set_size(play_orb_[i], sz, sz);
        lv_obj_set_style_radius(play_orb_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(play_orb_[i], (i % 2) ? kMagenta : kCyan, 0);
        lv_obj_set_style_bg_opa(play_orb_[i], LV_OPA_40, 0);
        lv_obj_set_pos(play_orb_[i], cx, cy);
        Track(play_objs_, &play_obj_n_, 28, play_orb_[i]);
    }

    // ── Assistant face: large symmetric eyes + mouth spectrum ────────────
    act_eye_w_ = (int16_t)(min_side * 0.27f);
    act_eye_h_ = (int16_t)(act_eye_w_ * 0.62f);
    act_pupil_r_ = (int16_t)(act_eye_h_ * 0.30f);
    act_eye_cy_ = (int16_t)(min_side * 0.40f);  // optical center of face pair
    const int16_t act_gap = (int16_t)(act_eye_w_ * 0.26f);
    BuildEye(&act_left_, (int16_t)(cx - act_eye_w_ / 2 - act_gap), act_eye_cy_, act_eye_w_,
             act_eye_h_, act_pupil_r_);
    BuildEye(&act_right_, (int16_t)(cx + act_eye_w_ / 2 + act_gap), act_eye_cy_, act_eye_w_,
             act_eye_h_, act_pupil_r_);
    Track(face_objs_, &face_obj_n_, 30, act_left_.body);
    Track(face_objs_, &face_obj_n_, 30, act_right_.body);

    // Mouth: compact equalizer under eyes (Sber-like "ротик").
    spectrum_base_y_ = (int16_t)(act_eye_cy_ + act_eye_h_ / 2 + min_side * 0.10f);
    const int bar_w = 14;
    const int bar_gap = 8;
    const int mouth_n = 12;
    const int mouth_total = mouth_n * bar_w + (mouth_n - 1) * bar_gap;
    const int mouth_ox = cx - mouth_total / 2;
    for (int i = 0; i < mouth_n; ++i) {
        spectrum_[i] = MakePlain(root_);
        lv_obj_set_size(spectrum_[i], bar_w, 18);
        // Mild smile curve: outer bars sit slightly higher.
        const float t = (i - (mouth_n - 1) * 0.5f) / ((mouth_n - 1) * 0.5f);
        const int smile = (int)(10.f * t * t);
        lv_obj_set_pos(spectrum_[i], mouth_ox + i * (bar_w + bar_gap), spectrum_base_y_ + smile);
        lv_obj_set_style_radius(spectrum_[i], bar_w / 2, 0);
        lv_obj_set_style_bg_color(spectrum_[i], kCyan, 0);
        lv_obj_set_style_bg_opa(spectrum_[i], LV_OPA_COVER, 0);
        Track(face_objs_, &face_obj_n_, 30, spectrum_[i]);
    }

    // ── Reply stage: ambient rings/orbs + large wrapped text, no frame ───
    reply_ring_ = MakePlain(root_);
    lv_obj_set_size(reply_ring_, min_side - 60, min_side - 60);
    lv_obj_center(reply_ring_);
    lv_obj_set_style_radius(reply_ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(reply_ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(reply_ring_, 16, 0);
    lv_obj_set_style_border_color(reply_ring_, kMagentaDim, 0);
    lv_obj_set_style_border_opa(reply_ring_, LV_OPA_20, 0);
    Track(reply_objs_, &reply_obj_n_, 20, reply_ring_);

    reply_ring2_ = MakePlain(root_);
    lv_obj_set_size(reply_ring2_, min_side - 130, min_side - 130);
    lv_obj_center(reply_ring2_);
    lv_obj_set_style_radius(reply_ring2_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(reply_ring2_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(reply_ring2_, 8, 0);
    lv_obj_set_style_border_color(reply_ring2_, kCyanDim, 0);
    lv_obj_set_style_border_opa(reply_ring2_, LV_OPA_20, 0);
    Track(reply_objs_, &reply_obj_n_, 20, reply_ring2_);

    for (int i = 0; i < 6; ++i) {
        reply_orb_[i] = MakePlain(root_);
        const int sz = (i % 2) ? 12 : 18;
        lv_obj_set_size(reply_orb_[i], sz, sz);
        lv_obj_set_style_radius(reply_orb_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(reply_orb_[i], (i % 2) ? kMagenta : kCyan, 0);
        lv_obj_set_style_bg_opa(reply_orb_[i], LV_OPA_30, 0);
        lv_obj_set_pos(reply_orb_[i], cx, cy);
        Track(reply_objs_, &reply_obj_n_, 20, reply_orb_[i]);
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* text_font =
        (lvgl_theme && lvgl_theme->text_font()) ? lvgl_theme->text_font()->font() : nullptr;
    // Board uses Noto 30 — large enough for round 466; wrap in center safe zone.
    const int text_w = (int)(min_side * 0.68f);
    caption_label_ = lv_label_create(root_);
    lv_label_set_text(caption_label_, "");
    lv_obj_set_width(caption_label_, text_w);
    lv_label_set_long_mode(caption_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(caption_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(caption_label_, kWhite, 0);
    lv_obj_set_style_text_line_space(caption_label_, 8, 0);
    if (text_font) lv_obj_set_style_text_font(caption_label_, text_font, 0);
    lv_obj_align(caption_label_, LV_ALIGN_CENTER, 0, 0);
    Track(reply_objs_, &reply_obj_n_, 20, caption_label_);

    idle_step_ = 0;
    ShowIdlePage(IdlePage::kClock);
    SetGroupHidden(face_objs_, face_obj_n_, true);
    SetGroupHidden(reply_objs_, reply_obj_n_, true);
    PickGazeTarget(play_eye_w_ * 0.18f, play_eye_h_ * 0.16f);
    next_blink_tick_ = 100 + (rand() % 120);
}

void ValeraLcdDisplay::TickThunk(lv_timer_t* timer) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->Tick();
}

void ValeraLcdDisplay::SetGroupHidden(lv_obj_t* const* objs, int n, bool hidden) {
    for (int i = 0; i < n; ++i) {
        if (!objs[i]) continue;
        if (hidden) lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }
}

bool ValeraLcdDisplay::InReplyStage() const {
    return assistant_active_ && has_caption_ && !hide_subtitle_;
}

void ValeraLcdDisplay::ShowIdlePage(IdlePage page) {
    idle_page_ = page;
    idle_page_ticks_ = 0;
    SetGroupHidden(clock_objs_, clock_obj_n_, page != IdlePage::kClock);
    SetGroupHidden(weather_objs_, weather_obj_n_, page != IdlePage::kWeather);
    SetGroupHidden(play_objs_, play_obj_n_, page != IdlePage::kPlay);
    if (page == IdlePage::kWeather) {
        last_out_temp_ = 9999;
        last_room_temp_ = 9999;
        last_weather_code_ = -999;
        PaintWeather();
    }
    if (page == IdlePage::kClock) {
        last_hour_ = -1;
        last_min_ = -1;
    }
}

void ValeraLcdDisplay::AdvanceIdlePage() {
    static const IdlePage kSeq[] = {IdlePage::kClock, IdlePage::kWeather, IdlePage::kClock,
                                    IdlePage::kWeather, IdlePage::kPlay};
    idle_step_ = (idle_step_ + 1) % 5;
    ShowIdlePage(kSeq[idle_step_]);
}

void ValeraLcdDisplay::LayoutForActive(bool active) {
    assistant_active_ = active;
    if (active) {
        SetGroupHidden(clock_objs_, clock_obj_n_, true);
        SetGroupHidden(weather_objs_, weather_obj_n_, true);
        SetGroupHidden(play_objs_, play_obj_n_, true);
        SyncActiveStage();
    } else {
        SetGroupHidden(face_objs_, face_obj_n_, true);
        SetGroupHidden(reply_objs_, reply_obj_n_, true);
        idle_step_ = 0;
        ShowIdlePage(IdlePage::kClock);
    }
}

void ValeraLcdDisplay::SyncActiveStage() {
    if (!assistant_active_) {
        SetGroupHidden(face_objs_, face_obj_n_, true);
        SetGroupHidden(reply_objs_, reply_obj_n_, true);
        return;
    }
    const bool reply = InReplyStage();
    SetGroupHidden(face_objs_, face_obj_n_, reply);
    SetGroupHidden(reply_objs_, reply_obj_n_, !reply);
}

void ValeraLcdDisplay::ApplyCaption() {
    if (!caption_label_) return;
    lv_label_set_text(caption_label_, caption_text_);
    has_caption_ = caption_text_[0] != '\0';
    SyncActiveStage();
}

void ValeraLcdDisplay::PickGazeTarget(float max_x, float max_y) {
    target_x_ = Frand(-max_x, max_x);
    target_y_ = Frand(-max_y, max_y);
    next_gaze_tick_ = tick_ + 50 + (rand() % 100);
}

void ValeraLcdDisplay::PlacePupil(Eye* eye, int16_t w, int16_t h, int16_t pupil_r, float look_x,
                                  float look_y, float open) {
    const int oh = (int)(h * Clampf(open, 0.08f, 1.f));
    lv_obj_set_height(eye->body, oh);
    lv_obj_set_y(eye->body, eye->home_cy - oh / 2);
    lv_obj_set_style_radius(eye->body, oh / 2, 0);

    float px = (float)w * 0.5f + look_x;
    float py = (float)oh * 0.5f + look_y;
    px = Clampf(px, pupil_r + 2.f, w - pupil_r - 2.f);
    py = Clampf(py, pupil_r + 2.f, oh - pupil_r - 2.f);
    lv_obj_set_pos(eye->pupil, (int)px - pupil_r, (int)py - pupil_r);
    lv_obj_set_pos(eye->shine, (int)px - pupil_r - 3, (int)py - pupil_r - 3);
}

void ValeraLcdDisplay::Tick() {
    if (!face_ready_ || sleepy_) return;
    ++tick_;

    if (caption_dirty_) {
        caption_dirty_ = false;
        ApplyCaption();
    }

    ActiveMode want = ActiveMode::kIdle;
    switch (Application::GetInstance().GetDeviceState()) {
        case kDeviceStateListening:
            want = ActiveMode::kListen;
            break;
        case kDeviceStateSpeaking:
            want = ActiveMode::kSpeak;
            break;
        case kDeviceStateIdle:
            want = ActiveMode::kIdle;
            break;
        case kDeviceStateConnecting:
        case kDeviceStateStarting:
        case kDeviceStateWifiConfiguring:
            want = ActiveMode::kWifi;
            break;
        default:
            want = ActiveMode::kThink;
            break;
    }
    if (want != active_mode_) SetActiveMode(want);

    AnimateAmbient();

    if (assistant_active_) {
        if (InReplyStage()) {
            AnimateReplyFx();
        } else {
            AnimateEyes(&act_left_, &act_right_, act_eye_w_, act_eye_h_, act_pupil_r_, false);
            AnimateSpectrum();
        }
        return;
    }

    ++idle_page_ticks_;
    int limit = kClockTicks;
    if (idle_page_ == IdlePage::kWeather) limit = kWeatherTicks;
    else if (idle_page_ == IdlePage::kPlay) limit = kPlayTicks;
    if (idle_page_ticks_ >= limit) AdvanceIdlePage();

    if (idle_page_ == IdlePage::kClock) {
        time_t now = time(nullptr);
        ValeraClimate climate = ValeraSensors::Get();
        if (climate.clock_fix_ok) now += climate.clock_fix_sec;
        struct tm tm_info = {};
        localtime_r(&now, &tm_info);
        const bool colon_on = ((tick_ / 30) % 2) == 0;
        if (tm_info.tm_hour != last_hour_ || tm_info.tm_min != last_min_ ||
            colon_on != last_colon_) {
            PaintClock(&tm_info, colon_on);
            last_hour_ = tm_info.tm_hour;
            last_min_ = tm_info.tm_min;
            last_colon_ = colon_on;
        }
    } else if (idle_page_ == IdlePage::kWeather) {
        if ((tick_ % 60) == 0) PaintWeather();
    } else {
        AnimateEyes(&play_left_, &play_right_, play_eye_w_, play_eye_h_, play_pupil_r_, true);
        AnimatePlayFx();
    }
}

void ValeraLcdDisplay::SetActiveMode(ActiveMode mode) {
    const bool was_active = assistant_active_;
    active_mode_ = mode;
    const bool now_active = mode != ActiveMode::kIdle && mode != ActiveMode::kSleep;
    if (now_active != was_active) LayoutForActive(now_active);
    else if (now_active) SyncActiveStage();
    if (mode == ActiveMode::kSleep) {
        LayoutForActive(false);
        ShowIdlePage(IdlePage::kClock);
        idle_step_ = 0;
    }
    PickGazeTarget(0.18f * (now_active ? act_eye_w_ : play_eye_w_),
                   0.16f * (now_active ? act_eye_h_ : play_eye_h_));
}

void ValeraLcdDisplay::AnimateAmbient() {
    const float breath = 0.5f + 0.5f * sinf(tick_ * 0.025f);
    lv_color_t rim = kCyanDim;
    lv_opa_t rim_opa = (lv_opa_t)(24 + (int)(40 * breath));
    if (assistant_active_) {
        if (InReplyStage()) {
            rim = kMagentaDim;
            rim_opa = (lv_opa_t)(8 + (int)(18 * breath));  // almost gone around text
        } else if (active_mode_ == ActiveMode::kListen) rim = kCyan;
        else if (active_mode_ == ActiveMode::kSpeak) rim = kMagenta;
        else if (active_mode_ == ActiveMode::kThink) rim = kThink;
        else if (active_mode_ == ActiveMode::kWifi) rim = ((tick_ / 28) % 2) ? kCyan : kMagenta;
    } else if (idle_page_ == IdlePage::kPlay) {
        rim = ((tick_ / 40) % 2) ? kCyanDim : kMagentaDim;
    }
    lv_obj_set_style_border_color(bezel_, rim, 0);
    lv_obj_set_style_border_opa(bezel_, rim_opa, 0);
}

void ValeraLcdDisplay::AnimateEyes(Eye* left, Eye* right, int16_t w, int16_t h, int16_t pupil_r,
                                   bool playful) {
    if (tick_ >= next_gaze_tick_) PickGazeTarget(w * (playful ? 0.22f : 0.18f), h * 0.16f);

    float bias_x = 0.f;
    float bias_y = 0.f;
    float speed = playful ? 0.08f : 0.12f;
    float want_squint = 0.f;
    lv_color_t eye_color = kEye;

    if (assistant_active_) {
        switch (active_mode_) {
            case ActiveMode::kListen:
                speed = 0.14f;
                eye_color = lv_color_hex(0xE8FCFF);
                break;
            case ActiveMode::kSpeak:
                speed = 0.12f;
                want_squint = 0.14f;
                eye_color = lv_color_hex(0xFFE8F6);
                break;
            case ActiveMode::kThink:
                speed = 0.06f;
                bias_x = ((tick_ / 100) % 2 == 0) ? -w * 0.14f : w * 0.14f;
                want_squint = 0.10f;
                eye_color = lv_color_hex(0xEEF0FF);
                break;
            case ActiveMode::kWifi:
                eye_color = ((tick_ / 28) % 2) ? lv_color_hex(0xE8FCFF) : lv_color_hex(0xFFE8F6);
                break;
            default:
                break;
        }
    } else if (playful) {
        // Sber Combine vibe: soft wash + happy squint pulses.
        const float wash = 0.5f + 0.5f * sinf(tick_ * 0.02f);
        eye_color = wash > 0.5f ? lv_color_hex(0xF5FBFF) : lv_color_hex(0xFFF5FB);
        if ((tick_ / 160) % 4 == 0) want_squint = 0.22f;
        // Gentle vertical bob of the whole pair.
        bias_y = 4.f * sinf(tick_ * 0.035f);
        speed = 0.07f;
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
            next_blink_tick_ = tick_ + (playful ? 70 : 110) + (rand() % 140);
        }
    }

    const float close = Clampf(blink_ > squint_ ? blink_ : squint_, 0.f, 1.f);
    const float open = 1.f - close * 0.92f;
    lv_obj_set_style_bg_color(left->body, eye_color, 0);
    lv_obj_set_style_bg_color(right->body, eye_color, 0);
    PlacePupil(left, w, h, pupil_r, look_x_, look_y_, open);
    PlacePupil(right, w, h, pupil_r, look_x_, look_y_, open);
}

void ValeraLcdDisplay::AnimatePlayFx() {
    const float cx = LV_HOR_RES * 0.5f;
    const float cy = LV_VER_RES * 0.5f;
    const float R = LV_HOR_RES * 0.36f;
    const float breath = 0.5f + 0.5f * sinf(tick_ * 0.03f);
    lv_obj_set_style_border_opa(play_ring_, (lv_opa_t)(14 + (int)(36 * breath)), 0);
    lv_obj_set_style_border_color(play_ring_, ((tick_ / 45) % 2) ? kCyanDim : kMagentaDim, 0);
    lv_obj_set_style_border_opa(play_ring2_, (lv_opa_t)(10 + (int)(30 * (1.f - breath))), 0);
    lv_obj_set_style_border_color(play_ring2_, ((tick_ / 45) % 2) ? kMagentaDim : kCyanDim, 0);

    for (int i = 0; i < 10; ++i) {
        const float a = tick_ * (0.016f + i * 0.0018f) + i * 0.75f;
        const float rx = R * (0.50f + 0.40f * ((i % 3) / 2.f));
        const float ry = R * (0.38f + 0.32f * (((i + 1) % 3) / 2.f));
        const int x = (int)(cx + rx * cosf(a));
        const int y = (int)(cy + ry * sinf(a * 0.88f + 0.35f));
        const int sz = (i % 3 == 0) ? 18 : ((i % 2) ? 10 : 14);
        lv_obj_set_pos(play_orb_[i], x - sz / 2, y - sz / 2);
        const float tw = 0.30f + 0.70f * fabsf(sinf(tick_ * 0.045f + i));
        lv_obj_set_style_bg_opa(play_orb_[i], (lv_opa_t)(28 + (int)(150 * tw)), 0);
    }
}

void ValeraLcdDisplay::AnimateSpectrum() {
    // Mouth ladder under the eyes — listening / thinking / speaking envelopes.
    for (int i = 0; i < 12; ++i) {
        float phase = tick_ * 0.24f + i * 0.48f;
        float env = 0.40f + 0.60f * fabsf(sinf(phase));
        if (active_mode_ == ActiveMode::kListen) env = 0.30f + 0.70f * fabsf(sinf(phase));
        else if (active_mode_ == ActiveMode::kThink)
            env = 0.20f + 0.35f * fabsf(sinf(phase * 0.35f));
        else if (active_mode_ == ActiveMode::kSpeak)
            env = 0.35f + 0.65f * fabsf(sinf(phase * 1.15f));
        const float t = (i - 5.5f) / 5.5f;
        const int smile = (int)(10.f * t * t);
        int h = 10 + (int)(56.f * env);
        lv_obj_set_height(spectrum_[i], h);
        lv_obj_set_y(spectrum_[i], spectrum_base_y_ + smile + 28 - h);
        lv_obj_set_style_bg_color(spectrum_[i], (i % 2) ? kMagenta : kCyan, 0);
        lv_obj_set_style_bg_opa(spectrum_[i], (lv_opa_t)(150 + (int)(90 * env)), 0);
    }
}

void ValeraLcdDisplay::AnimateReplyFx() {
    // No ring/frame around the text — only sparse edge orbs for AMOLED life.
    if (reply_ring_) lv_obj_set_style_border_opa(reply_ring_, LV_OPA_TRANSP, 0);
    if (reply_ring2_) lv_obj_set_style_border_opa(reply_ring2_, LV_OPA_TRANSP, 0);

    const float cx = LV_HOR_RES * 0.5f;
    const float cy = LV_VER_RES * 0.5f;
    const float R = LV_HOR_RES * 0.42f;
    for (int i = 0; i < 6; ++i) {
        const float a = tick_ * (0.010f + i * 0.0016f) + i * 1.05f;
        const float rx = R * (0.78f + 0.08f * (i % 2));
        const float ry = R * (0.78f + 0.08f * ((i + 1) % 2));
        const int sz = (i % 2) ? 10 : 16;
        const int x = (int)(cx + rx * cosf(a));
        const int y = (int)(cy + ry * sinf(a * 0.92f));
        lv_obj_set_pos(reply_orb_[i], x - sz / 2, y - sz / 2);
        const float tw = 0.30f + 0.70f * fabsf(sinf(tick_ * 0.03f + i));
        lv_obj_set_style_bg_opa(reply_orb_[i], (lv_opa_t)(16 + (int)(90 * tw)), 0);
    }
}

void ValeraLcdDisplay::PaintClock(const struct tm* tm, bool colon_on) {
    if (tm == nullptr) return;
    int digits[4] = {tm->tm_hour / 10, tm->tm_hour % 10, tm->tm_min / 10, tm->tm_min % 10};
    for (int d = 0; d < 4; ++d) LitDigit(clock_seg_[d], digits[d], kCyan);
    const lv_opa_t opa = colon_on ? LV_OPA_COVER : LV_OPA_30;
    lv_obj_set_style_bg_opa(clock_colon_[0], opa, 0);
    lv_obj_set_style_bg_opa(clock_colon_[1], opa, 0);
}

void ValeraLcdDisplay::PaintWeather() {
    ValeraClimate c = ValeraSensors::Get();
    int out = c.outdoor_ok ? (int)lroundf(c.outdoor_c) : 0;
    int room = c.room_ok ? (int)lroundf(c.room_c) : 0;
    if (!c.outdoor_ok && !c.room_ok) {
        out = 0;
        room = 0;
    }

    if (out != last_out_temp_ || c.weather_code != last_weather_code_) {
        last_out_temp_ = out;
        last_weather_code_ = c.weather_code;
        const bool neg = out < 0;
        int absv = neg ? -out : out;
        if (absv > 99) absv = 99;
        lv_obj_set_style_bg_opa(weather_out_minus_, neg ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(weather_out_minus_, kCyan, 0);
        LitDigit(weather_out_seg_[0], absv / 10, kCyan);
        LitDigit(weather_out_seg_[1], absv % 10, kCyan);

        lv_color_t g = kSun;
        if (c.weather_code >= 51) g = kThink;
        else if (c.weather_code >= 3) g = kCloud;
        else if (c.weather_code >= 1) g = kSun;
        lv_obj_set_style_bg_color(weather_glyph_, g, 0);
    }

    if (room != last_room_temp_) {
        last_room_temp_ = room;
        int absr = room < 0 ? -room : room;
        if (absr > 99) absr = 99;
        LitDigit(weather_room_seg_[0], absr / 10, kMagenta);
        LitDigit(weather_room_seg_[1], absr % 10, kMagenta);
    }
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
    if (std::strcmp(emotion, "thinking") == 0) SetActiveMode(ActiveMode::kThink);
    else if (std::strcmp(emotion, "happy") == 0 || std::strcmp(emotion, "laughing") == 0)
        SetActiveMode(ActiveMode::kSpeak);
    else if (std::strcmp(emotion, "sleepy") == 0) SetActiveMode(ActiveMode::kSleep);
}

void ValeraLcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (!face_ready_ || caption_label_ == nullptr) return;

    const char* text = (content != nullptr) ? content : "";
    std::strncpy(caption_text_, text, sizeof(caption_text_) - 1);
    caption_text_[sizeof(caption_text_) - 1] = '\0';

    lv_color_t color = kWhite;
    if (role != nullptr) {
        if (std::strcmp(role, "user") == 0) color = kCyan;
        else if (std::strcmp(role, "assistant") == 0) color = kWhite;
        else if (std::strcmp(role, "system") == 0) color = kThink;
    }
    lv_obj_set_style_text_color(caption_label_, color, 0);

    caption_dirty_ = true;
    ApplyCaption();
}

void ValeraLcdDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    caption_text_[0] = '\0';
    has_caption_ = false;
    caption_dirty_ = false;
    if (caption_label_) lv_label_set_text(caption_label_, "");
    SyncActiveStage();
}

void ValeraLcdDisplay::SetHideSubtitle(bool hide) {
    DisplayLockGuard lock(this);
    hide_subtitle_ = hide;
    SyncActiveStage();
}

void ValeraLcdDisplay::UpdateStatusBar(bool update_all) {
    (void)update_all;
}

void ValeraLcdDisplay::SetPowerSaveMode(bool on) {
    sleepy_ = on;
    if (on) SetActiveMode(ActiveMode::kSleep);
    else if (face_ready_) SetActiveMode(ActiveMode::kIdle);
}

void ValeraLcdDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);
    DisplayLockGuard lock(this);
    if (!caption_label_ || theme == nullptr) return;
    auto* lvgl_theme = static_cast<LvglTheme*>(theme);
    if (lvgl_theme->text_font()) {
        lv_obj_set_style_text_font(caption_label_, lvgl_theme->text_font()->font(), 0);
    }
}
