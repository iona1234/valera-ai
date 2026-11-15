#include "valera_face.h"

#include "application.h"
#include "device_state.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "settings.h"
#include "valera_sensors.h"

#include <wifi_manager.h>

#include <cmath>
#include <cstdio>
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
const lv_color_t kCoral = lv_color_hex(0xFF8FA3);
const lv_color_t kEdge = lv_color_hex(0x9AB4C8);
const lv_color_t kSun = lv_color_hex(0xFFD36A);
const lv_color_t kCloud = lv_color_hex(0xC8D6E5);
const lv_color_t kOrange = lv_color_hex(0xFF8A3D);

const char* WeatherWord(int code) {
    if (code >= 80) return "ЛИВНИ";
    if (code >= 61) return "ДОЖДЬ";
    if (code >= 51) return "МОРОСЬ";
    if (code >= 45) return "ТУМАН";
    if (code >= 3) return "ОБЛАЧНО";
    if (code >= 1) return "ЯСНО";
    return "ЧИСТО";
}

const char* Weekday3(int wday) {
    static const char* kD[] = {"ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ"};
    if (wday < 0 || wday > 6) return "";
    return kD[wday];
}

const char* Month3(int mon) {
    static const char* kM[] = {"ЯНВ", "ФЕВ", "МАР", "АПР", "МАЙ", "ИЮН",
                               "ИЮЛ", "АВГ", "СЕН", "ОКТ", "НОЯ", "ДЕК"};
    if (mon < 0 || mon > 11) return "";
    return kM[mon];
}

lv_obj_t* MakePlain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
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
    LoadClockStyle();
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

void ValeraLcdDisplay::BuildSpectrum(lv_obj_t** bars, lv_obj_t** track, int* track_n, int track_cap,
                                     int cy, lv_color_t color) {
    const int W = LV_HOR_RES;
    const int cx = W / 2;
    const int bar_w = 12;
    const int bar_gap = 7;
    const int total = kSpectrumN * bar_w + (kSpectrumN - 1) * bar_gap;
    const int ox = cx - total / 2;
    for (int i = 0; i < kSpectrumN; ++i) {
        bars[i] = MakePlain(root_);
        lv_obj_set_size(bars[i], bar_w, 16);
        lv_obj_set_pos(bars[i], ox + i * (bar_w + bar_gap), cy - 8);
        lv_obj_set_style_radius(bars[i], bar_w / 2, 0);
        lv_obj_set_style_bg_color(bars[i], color, 0);
        lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, 0);
        Track(track, track_n, track_cap, bars[i]);
    }
}

void ValeraLcdDisplay::LoadClockStyle() {
    Settings s("valera", false);
    clock_look_ = s.GetInt("clock_look", 0);
    clock_scale_pct_ = s.GetInt("clock_scale", 100);
    clock_hh_color_ = (uint32_t)s.GetInt("clock_hh", 0xFFFFFF);
    clock_mm_color_ = (uint32_t)s.GetInt("clock_mm", 0xFF8FA3);
    if (clock_scale_pct_ < 80) clock_scale_pct_ = 80;
    if (clock_scale_pct_ > 160) clock_scale_pct_ = 160;
}

void ValeraLcdDisplay::SetClockStyle(int look, int scale_pct, uint32_t hh_color, uint32_t mm_color) {
    clock_look_ = look;
    clock_scale_pct_ = scale_pct;
    clock_hh_color_ = hh_color;
    clock_mm_color_ = mm_color;
    Settings s("valera", true);
    s.SetInt("clock_look", clock_look_);
    s.SetInt("clock_scale", clock_scale_pct_);
    s.SetInt("clock_hh", (int32_t)clock_hh_color_);
    s.SetInt("clock_mm", (int32_t)clock_mm_color_);
    DisplayLockGuard lock(this);
    ApplyClockStyleVisual();
}

void ValeraLcdDisplay::ApplyClockStyleVisual() {
    if (!time_hh_label_ || !time_mm_label_) return;
    float mult = 5.5f * (clock_scale_pct_ / 100.f);
    if (clock_look_ == 1) mult *= 1.05f;  // Neon — slightly larger
    if (clock_look_ == 2) mult *= 0.95f;  // Soft — a bit smaller
    const int32_t zoom = (int32_t)(LV_SCALE_NONE * mult);
    lv_obj_set_style_transform_scale_x(time_hh_label_, zoom, 0);
    lv_obj_set_style_transform_scale_y(time_hh_label_, zoom, 0);
    lv_obj_set_style_transform_scale_x(time_mm_label_, zoom, 0);
    lv_obj_set_style_transform_scale_y(time_mm_label_, zoom, 0);

    lv_color_t hh = lv_color_hex(clock_hh_color_);
    lv_color_t mm = lv_color_hex(clock_mm_color_);
    if (clock_look_ == 1) {  // Neon
        hh = kCyan;
        mm = kMagenta;
    } else if (clock_look_ == 2) {  // Soft — both coral-ish
        hh = lv_color_hex(0xFFE8EE);
        mm = kCoral;
    }
    lv_obj_set_style_text_color(time_hh_label_, hh, 0);
    lv_obj_set_style_text_color(time_mm_label_, mm, 0);
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

    bezel_ = MakePlain(root_);
    lv_obj_set_size(bezel_, min_side - 16, min_side - 16);
    lv_obj_center(bezel_);
    lv_obj_set_style_radius(bezel_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(bezel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bezel_, 2, 0);
    lv_obj_set_style_border_color(bezel_, kCyanDim, 0);
    lv_obj_set_style_border_opa(bezel_, LV_OPA_30, 0);

    auto* lvgl_theme_clk = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* edge_font =
        (lvgl_theme_clk && lvgl_theme_clk->text_font()) ? lvgl_theme_clk->text_font()->font()
                                                       : nullptr;

    auto make_stack = [&](lv_color_t color) -> lv_obj_t* {
        lv_obj_t* lab = lv_label_create(root_);
        lv_label_set_text(lab, "00");
        lv_obj_set_style_text_color(lab, color, 0);
        lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
        if (edge_font) lv_obj_set_style_text_font(lab, edge_font, 0);
        lv_obj_set_style_transform_pivot_x(lab, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(lab, lv_pct(50), 0);
        return lab;
    };
    time_hh_label_ = make_stack(kWhite);
    time_mm_label_ = make_stack(kCoral);
    lv_obj_align(time_hh_label_, LV_ALIGN_CENTER, 0, -(int)(min_side * 0.17f));
    lv_obj_align(time_mm_label_, LV_ALIGN_CENTER, 0, (int)(min_side * 0.17f));
    Track(clock_objs_, &clock_obj_n_, 16, time_hh_label_);
    Track(clock_objs_, &clock_obj_n_, 16, time_mm_label_);
    ApplyClockStyleVisual();

    clock_edge_weather_ = lv_label_create(root_);
    lv_label_set_text(clock_edge_weather_, "");
    lv_obj_set_style_text_color(clock_edge_weather_, kCoral, 0);
    lv_obj_set_style_text_align(clock_edge_weather_, LV_TEXT_ALIGN_CENTER, 0);
    if (edge_font) lv_obj_set_style_text_font(clock_edge_weather_, edge_font, 0);
    lv_obj_align(clock_edge_weather_, LV_ALIGN_TOP_MID, 0, (int)(min_side * 0.04f));
    Track(clock_objs_, &clock_obj_n_, 16, clock_edge_weather_);

    clock_edge_date_ = lv_label_create(root_);
    lv_label_set_text(clock_edge_date_, "");
    lv_obj_set_style_text_color(clock_edge_date_, kEdge, 0);
    lv_obj_set_style_text_align(clock_edge_date_, LV_TEXT_ALIGN_CENTER, 0);
    if (edge_font) lv_obj_set_style_text_font(clock_edge_date_, edge_font, 0);
    lv_obj_align(clock_edge_date_, LV_ALIGN_BOTTOM_MID, 0, -(int)(min_side * 0.05f));
    Track(clock_objs_, &clock_obj_n_, 16, clock_edge_date_);

    // Weather — large labels
    weather_glyph_ = MakePlain(root_);
    lv_obj_set_size(weather_glyph_, 56, 56);
    lv_obj_set_pos(weather_glyph_, cx - 28, cy - 150);
    lv_obj_set_style_radius(weather_glyph_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(weather_glyph_, kSun, 0);
    lv_obj_set_style_bg_opa(weather_glyph_, LV_OPA_COVER, 0);
    Track(weather_objs_, &weather_obj_n_, 16, weather_glyph_);

    weather_out_label_ = lv_label_create(root_);
    lv_label_set_text(weather_out_label_, "--°");
    lv_obj_set_style_text_color(weather_out_label_, kCyan, 0);
    lv_obj_set_style_text_align(weather_out_label_, LV_TEXT_ALIGN_CENTER, 0);
    if (edge_font) lv_obj_set_style_text_font(weather_out_label_, edge_font, 0);
    lv_obj_set_style_transform_pivot_x(weather_out_label_, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(weather_out_label_, lv_pct(50), 0);
    lv_obj_set_style_transform_scale_x(weather_out_label_, LV_SCALE_NONE * 3, 0);
    lv_obj_set_style_transform_scale_y(weather_out_label_, LV_SCALE_NONE * 3, 0);
    lv_obj_align(weather_out_label_, LV_ALIGN_CENTER, 0, -10);
    Track(weather_objs_, &weather_obj_n_, 16, weather_out_label_);

    weather_room_label_ = lv_label_create(root_);
    lv_label_set_text(weather_room_label_, "");
    lv_obj_set_style_text_color(weather_room_label_, kMagenta, 0);
    lv_obj_set_style_text_align(weather_room_label_, LV_TEXT_ALIGN_CENTER, 0);
    if (edge_font) lv_obj_set_style_text_font(weather_room_label_, edge_font, 0);
    lv_obj_align(weather_room_label_, LV_ALIGN_CENTER, 0, 70);
    Track(weather_objs_, &weather_obj_n_, 16, weather_room_label_);

    weather_cond_label_ = lv_label_create(root_);
    lv_label_set_text(weather_cond_label_, "");
    lv_obj_set_style_text_color(weather_cond_label_, kEdge, 0);
    lv_obj_set_style_text_align(weather_cond_label_, LV_TEXT_ALIGN_CENTER, 0);
    if (edge_font) lv_obj_set_style_text_font(weather_cond_label_, edge_font, 0);
    lv_obj_align(weather_cond_label_, LV_ALIGN_CENTER, 0, 110);
    Track(weather_objs_, &weather_obj_n_, 16, weather_cond_label_);

    // Play / Sber screensaver
    play_eye_w_ = (int16_t)(min_side * 0.28f);
    play_eye_h_ = (int16_t)(play_eye_w_ * 0.62f);
    play_pupil_r_ = (int16_t)(play_eye_h_ * 0.28f);
    const int16_t play_gap = (int16_t)(play_eye_w_ * 0.24f);
    BuildEye(&play_left_, (int16_t)(cx - play_eye_w_ / 2 - play_gap), (int16_t)(cy - 10),
             play_eye_w_, play_eye_h_, play_pupil_r_);
    BuildEye(&play_right_, (int16_t)(cx + play_eye_w_ / 2 + play_gap), (int16_t)(cy - 10),
             play_eye_w_, play_eye_h_, play_pupil_r_);
    Track(play_objs_, &play_obj_n_, 28, play_left_.body);
    Track(play_objs_, &play_obj_n_, 28, play_right_.body);

    play_ring_ = MakePlain(root_);
    lv_obj_set_size(play_ring_, min_side - 40, min_side - 40);
    lv_obj_center(play_ring_);
    lv_obj_set_style_radius(play_ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(play_ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(play_ring_, 10, 0);
    lv_obj_set_style_border_color(play_ring_, kCyanDim, 0);
    lv_obj_set_style_border_opa(play_ring_, LV_OPA_20, 0);
    Track(play_objs_, &play_obj_n_, 28, play_ring_);

    play_ring2_ = MakePlain(root_);
    lv_obj_set_size(play_ring2_, min_side - 100, min_side - 100);
    lv_obj_center(play_ring2_);
    lv_obj_set_style_radius(play_ring2_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(play_ring2_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(play_ring2_, 6, 0);
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

    // Assistant face
    act_eye_w_ = (int16_t)(min_side * 0.27f);
    act_eye_h_ = (int16_t)(act_eye_w_ * 0.62f);
    act_pupil_r_ = (int16_t)(act_eye_h_ * 0.30f);
    act_eye_cy_ = (int16_t)(min_side * 0.40f);
    const int16_t act_gap = (int16_t)(act_eye_w_ * 0.26f);
    BuildEye(&act_left_, (int16_t)(cx - act_eye_w_ / 2 - act_gap), act_eye_cy_, act_eye_w_,
             act_eye_h_, act_pupil_r_);
    BuildEye(&act_right_, (int16_t)(cx + act_eye_w_ / 2 + act_gap), act_eye_cy_, act_eye_w_,
             act_eye_h_, act_pupil_r_);
    Track(face_objs_, &face_obj_n_, 40, act_left_.body);
    Track(face_objs_, &face_obj_n_, 40, act_right_.body);

    spectrum_center_y_ = (int16_t)(act_eye_cy_ + act_eye_h_ / 2 + min_side * 0.16f);
    BuildSpectrum(spectrum_, face_objs_, &face_obj_n_, 40, spectrum_center_y_, kCyan);

    // Reply: orbs + voice EQ + caption
    for (int i = 0; i < 6; ++i) {
        reply_orb_[i] = MakePlain(root_);
        const int sz = (i % 2) ? 12 : 18;
        lv_obj_set_size(reply_orb_[i], sz, sz);
        lv_obj_set_style_radius(reply_orb_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(reply_orb_[i], (i % 2) ? kMagenta : kCoral, 0);
        lv_obj_set_style_bg_opa(reply_orb_[i], LV_OPA_30, 0);
        lv_obj_set_pos(reply_orb_[i], cx, cy);
        Track(reply_objs_, &reply_obj_n_, 40, reply_orb_[i]);
    }

    voice_eq_cy_ = (int16_t)(min_side * 0.82f);
    BuildSpectrum(voice_eq_, reply_objs_, &reply_obj_n_, 40, voice_eq_cy_, kCoral);

    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* text_font =
        (lvgl_theme && lvgl_theme->text_font()) ? lvgl_theme->text_font()->font() : nullptr;
    const int text_w = (int)(min_side * 0.68f);
    caption_label_ = lv_label_create(root_);
    lv_label_set_text(caption_label_, "");
    lv_obj_set_width(caption_label_, text_w);
    lv_label_set_long_mode(caption_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(caption_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(caption_label_, kWhite, 0);
    lv_obj_set_style_text_line_space(caption_label_, 8, 0);
    if (text_font) lv_obj_set_style_text_font(caption_label_, text_font, 0);
    lv_obj_align(caption_label_, LV_ALIGN_CENTER, 0, -20);
    Track(reply_objs_, &reply_obj_n_, 40, caption_label_);

    // Wi-Fi screen
    wifi_title_ = lv_label_create(root_);
    lv_label_set_text(wifi_title_, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_title_, kOrange, 0);
    lv_obj_set_style_text_align(wifi_title_, LV_TEXT_ALIGN_CENTER, 0);
    if (text_font) lv_obj_set_style_text_font(wifi_title_, text_font, 0);
    lv_obj_align(wifi_title_, LV_ALIGN_TOP_MID, 0, (int)(min_side * 0.12f));
    Track(wifi_objs_, &wifi_obj_n_, 12, wifi_title_);

    wifi_ssid_ = lv_label_create(root_);
    lv_label_set_text(wifi_ssid_, "Xiaozhi");
    lv_obj_set_width(wifi_ssid_, text_w);
    lv_label_set_long_mode(wifi_ssid_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(wifi_ssid_, kWhite, 0);
    lv_obj_set_style_text_align(wifi_ssid_, LV_TEXT_ALIGN_CENTER, 0);
    if (text_font) lv_obj_set_style_text_font(wifi_ssid_, text_font, 0);
    lv_obj_align(wifi_ssid_, LV_ALIGN_CENTER, 0, -30);
    Track(wifi_objs_, &wifi_obj_n_, 12, wifi_ssid_);

    wifi_hint_ = lv_label_create(root_);
    lv_label_set_text(wifi_hint_, "Подключитесь к точке\nи откройте страницу\nв браузере");
    lv_obj_set_width(wifi_hint_, text_w);
    lv_label_set_long_mode(wifi_hint_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(wifi_hint_, kEdge, 0);
    lv_obj_set_style_text_align(wifi_hint_, LV_TEXT_ALIGN_CENTER, 0);
    if (text_font) lv_obj_set_style_text_font(wifi_hint_, text_font, 0);
    lv_obj_align(wifi_hint_, LV_ALIGN_CENTER, 0, 80);
    Track(wifi_objs_, &wifi_obj_n_, 12, wifi_hint_);

    idle_step_ = 0;
    ShowIdlePage(IdlePage::kClock);
    SetGroupHidden(face_objs_, face_obj_n_, true);
    SetGroupHidden(reply_objs_, reply_obj_n_, true);
    SetGroupHidden(wifi_objs_, wifi_obj_n_, true);
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

bool ValeraLcdDisplay::InWifiStage() const { return active_mode_ == ActiveMode::kWifi; }

bool ValeraLcdDisplay::InReplyStage() const {
    // Speak = voice-assistant UI (coral EQ + text). Listen/think keep eyes + mouth EQ.
    return assistant_active_ && active_mode_ == ActiveMode::kSpeak && !hide_subtitle_ &&
           !InWifiStage();
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
        SetGroupHidden(wifi_objs_, wifi_obj_n_, true);
        idle_step_ = 0;
        ShowIdlePage(IdlePage::kClock);
    }
}

void ValeraLcdDisplay::SyncActiveStage() {
    if (!assistant_active_) {
        SetGroupHidden(face_objs_, face_obj_n_, true);
        SetGroupHidden(reply_objs_, reply_obj_n_, true);
        SetGroupHidden(wifi_objs_, wifi_obj_n_, true);
        return;
    }
    if (InWifiStage()) {
        SetGroupHidden(face_objs_, face_obj_n_, true);
        SetGroupHidden(reply_objs_, reply_obj_n_, true);
        SetGroupHidden(wifi_objs_, wifi_obj_n_, false);
        PaintWifiHint();
        return;
    }
    const bool reply = InReplyStage();
    SetGroupHidden(wifi_objs_, wifi_obj_n_, true);
    SetGroupHidden(face_objs_, face_obj_n_, reply);
    SetGroupHidden(reply_objs_, reply_obj_n_, !reply);
}

void ValeraLcdDisplay::ApplyCaption() {
    if (!caption_label_) return;
    lv_label_set_text(caption_label_, caption_text_);
    has_caption_ = caption_text_[0] != '\0';
    SyncActiveStage();
}

void ValeraLcdDisplay::PaintWifiHint() {
    auto& wifi = WifiManager::GetInstance();
    char ssid[64];
    if (wifi.IsConfigMode()) {
        snprintf(ssid, sizeof(ssid), "%s", wifi.GetApSsid().c_str());
        if (wifi_ssid_) lv_label_set_text(wifi_ssid_, ssid);
        char hint[192];
        snprintf(hint, sizeof(hint), "1) Wi-Fi → %s\n2) Браузер → %s", ssid,
                 wifi.GetApWebUrl().c_str());
        if (wifi_hint_) lv_label_set_text(wifi_hint_, hint);
    } else if (caption_text_[0]) {
        if (wifi_ssid_) lv_label_set_text(wifi_ssid_, caption_text_);
    }
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
        if (InWifiStage()) {
            if ((tick_ % 60) == 0) PaintWifiHint();
            return;
        }
        if (InReplyStage()) {
            AnimateReplyFx();
            // Living coral voice EQ while assistant speaks (different UI from listen).
            AnimateSpectrum(voice_eq_, 0.95f, 0.40f, kCoral, kMagenta);
        } else {
            AnimateEyes(&act_left_, &act_right_, act_eye_w_, act_eye_h_, act_pupil_r_, false);
            if (active_mode_ == ActiveMode::kListen)
                AnimateSpectrum(spectrum_, 0.90f, 0.34f, kCyan, kCyan);
            else if (active_mode_ == ActiveMode::kThink)
                AnimateSpectrum(spectrum_, 0.40f, 0.14f, kThink, kMagenta);
            else
                AnimateSpectrum(spectrum_, 0.70f, 0.28f, kMagenta, kCoral);
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
        if (tm_info.tm_hour != last_hour_ || tm_info.tm_min != last_min_ ||
            tm_info.tm_wday != last_wday_ || tm_info.tm_mday != last_mday_ ||
            (tick_ % 120) == 0) {
            PaintClock(&tm_info, false);
            last_hour_ = tm_info.tm_hour;
            last_min_ = tm_info.tm_min;
            last_wday_ = tm_info.tm_wday;
            last_mday_ = tm_info.tm_mday;
        }
    } else if (idle_page_ == IdlePage::kWeather) {
        if ((tick_ % 60) == 0) PaintWeather();
        // Soft pulse on glyph
        const float breath = 0.5f + 0.5f * sinf(tick_ * 0.04f);
        lv_obj_set_style_bg_opa(weather_glyph_, (lv_opa_t)(180 + (int)(70 * breath)), 0);
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
        if (InWifiStage()) {
            rim = ((tick_ / 28) % 2) ? kOrange : kCyan;
            rim_opa = (lv_opa_t)(40 + (int)(50 * breath));
        } else if (InReplyStage()) {
            rim = kMagentaDim;
            rim_opa = (lv_opa_t)(8 + (int)(18 * breath));
        } else if (active_mode_ == ActiveMode::kListen)
            rim = kCyan;
        else if (active_mode_ == ActiveMode::kSpeak)
            rim = kMagenta;
        else if (active_mode_ == ActiveMode::kThink)
            rim = kThink;
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

    // Emotion overlays (do not change ActiveMode — device state owns that).
    const bool happy = std::strcmp(emotion_name_, "happy") == 0 ||
                       std::strcmp(emotion_name_, "laughing") == 0;
    const bool sad = std::strcmp(emotion_name_, "sad") == 0 ||
                     std::strcmp(emotion_name_, "crying") == 0;
    const bool surprised = std::strcmp(emotion_name_, "surprised") == 0 ||
                           std::strcmp(emotion_name_, "shocked") == 0;

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
            default:
                break;
        }
    } else if (playful) {
        // Sber payment-terminal vibe: dual rings + orbs (AnimatePlayFx) + soft eye wash.
        const float wash = 0.5f + 0.5f * sinf(tick_ * 0.02f);
        eye_color = wash > 0.5f ? lv_color_hex(0xF5FBFF) : lv_color_hex(0xFFF5FB);
        if ((tick_ / 160) % 4 == 0) want_squint = 0.22f;
        bias_y = 4.f * sinf(tick_ * 0.035f);
        speed = 0.07f;
    }

    if (happy) want_squint = Clampf(want_squint + 0.18f, 0.f, 0.45f);
    if (sad) {
        bias_y += h * 0.08f;
        want_squint = Clampf(want_squint + 0.12f, 0.f, 0.4f);
    }
    if (surprised) {
        want_squint = 0.f;
        bias_y -= h * 0.04f;
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

void ValeraLcdDisplay::AnimateSpectrum(lv_obj_t* const* bars, float drive, float speed,
                                       lv_color_t c0, lv_color_t c1) {
    // Perfect L↔R mirror around center bar (kSpectrumN is odd).
    constexpr float kHalfPi = 1.57079637f;
    const int mid = kSpectrumN / 2;
    for (int i = 0; i < kSpectrumN; ++i) {
        if (!bars[i]) continue;
        const int mirror = (i <= mid) ? i : (kSpectrumN - 1 - i);
        const float shape = 0.22f + 0.78f * sinf((mirror + 0.5f) / (mid + 1.f) * kHalfPi);
        const float wave =
            0.45f + 0.55f * fabsf(sinf(tick_ * speed + mirror * 0.70f + 0.15f * sinf(tick_ * 0.07f)));
        const float env = Clampf(shape * (0.35f + drive * wave), 0.12f, 1.f);
        const int h = 12 + (int)(64.f * env);
        const int cy = (bars == spectrum_) ? spectrum_center_y_ : voice_eq_cy_;
        lv_obj_set_height(bars[i], h);
        lv_obj_set_y(bars[i], cy - h / 2);
        lv_obj_set_style_bg_color(bars[i], (mirror % 2) ? c1 : c0, 0);
        lv_obj_set_style_bg_opa(bars[i], (lv_opa_t)(150 + (int)(95 * env)), 0);
    }
}

void ValeraLcdDisplay::AnimateReplyFx() {
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
    (void)colon_on;
    if (tm == nullptr) return;
    char hh[4];
    char mm[4];
    snprintf(hh, sizeof(hh), "%02d", tm->tm_hour);
    snprintf(mm, sizeof(mm), "%02d", tm->tm_min);
    if (time_hh_label_) lv_label_set_text(time_hh_label_, hh);
    if (time_mm_label_) lv_label_set_text(time_mm_label_, mm);

    ValeraClimate c = ValeraSensors::Get();
    char weather_line[48];
    if (c.outdoor_ok) {
        snprintf(weather_line, sizeof(weather_line), "%d° %s", (int)lroundf(c.outdoor_c),
                 WeatherWord(c.weather_code));
    } else {
        snprintf(weather_line, sizeof(weather_line), "%s", WeatherWord(c.weather_code));
    }
    if (clock_edge_weather_) lv_label_set_text(clock_edge_weather_, weather_line);

    char date_line[48];
    snprintf(date_line, sizeof(date_line), "%s %d %s", Weekday3(tm->tm_wday), tm->tm_mday,
             Month3(tm->tm_mon));
    if (clock_edge_date_) lv_label_set_text(clock_edge_date_, date_line);
}

void ValeraLcdDisplay::PaintWeather() {
    ValeraClimate c = ValeraSensors::Get();
    int out = c.outdoor_ok ? (int)lroundf(c.outdoor_c) : 0;
    int room = c.room_ok ? (int)lroundf(c.room_c) : 0;

    if (out != last_out_temp_ || c.weather_code != last_weather_code_) {
        last_out_temp_ = out;
        last_weather_code_ = c.weather_code;
        char buf[24];
        if (c.outdoor_ok)
            snprintf(buf, sizeof(buf), "%d°", out);
        else
            snprintf(buf, sizeof(buf), "--°");
        if (weather_out_label_) lv_label_set_text(weather_out_label_, buf);
        if (weather_cond_label_) lv_label_set_text(weather_cond_label_, WeatherWord(c.weather_code));

        lv_color_t g = kSun;
        if (c.weather_code >= 51)
            g = kThink;
        else if (c.weather_code >= 3)
            g = kCloud;
        lv_obj_set_style_bg_color(weather_glyph_, g, 0);
    }

    if (room != last_room_temp_) {
        last_room_temp_ = room;
        char buf[32];
        if (c.room_ok)
            snprintf(buf, sizeof(buf), "дома %d°", room);
        else
            snprintf(buf, sizeof(buf), "BME —");
        if (weather_room_label_) lv_label_set_text(weather_room_label_, buf);
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
    // Emotions only drive eye animation — never override device-state modes.
    if (emotion == nullptr) emotion = "neutral";
    std::strncpy(emotion_name_, emotion, sizeof(emotion_name_) - 1);
    emotion_name_[sizeof(emotion_name_) - 1] = '\0';
}

void ValeraLcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (!face_ready_ || caption_label_ == nullptr) return;

    const char* text = (content != nullptr) ? content : "";
    std::strncpy(caption_text_, text, sizeof(caption_text_) - 1);
    caption_text_[sizeof(caption_text_) - 1] = '\0';

    lv_color_t color = kWhite;
    if (role != nullptr) {
        if (std::strcmp(role, "user") == 0)
            color = kCyan;
        else if (std::strcmp(role, "assistant") == 0)
            color = kWhite;
        else if (std::strcmp(role, "system") == 0)
            color = kThink;
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

void ValeraLcdDisplay::UpdateStatusBar(bool update_all) { (void)update_all; }

void ValeraLcdDisplay::SetPowerSaveMode(bool on) {
    sleepy_ = on;
    if (on)
        SetActiveMode(ActiveMode::kSleep);
    else if (face_ready_)
        SetActiveMode(ActiveMode::kIdle);
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
