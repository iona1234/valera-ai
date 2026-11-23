#include "valera_face.h"

#include "application.h"
#include "assets/lang_config.h"
#include "audio/audio_codec.h"
#include "board.h"
#include "device_state.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "settings.h"
#include "valera_radio_prefs.h"
#include "valera_radio_stations.h"
#include "valera_sensors.h"
#include "wifi_board.h"

#include <esp_log.h>
#include <ssid_manager.h>
#include <wifi_manager.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

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
const lv_color_t kCard = lv_color_hex(0x1A2430);
const lv_color_t kCardHi = lv_color_hex(0x2A3848);

const char* WeatherWord(int code) {
    if (code < 0) return "";
    if (code == 0 || code == 1) return "ясно";
    if (code == 2 || code == 3) return "облачно";
    if (code == 45 || code == 48) return "туман";
    if (code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86) return "снег";
    if (code == 95 || code == 96 || code == 99) return "гроза";
    if (code >= 80) return "ливни";
    if (code >= 61) return "дождь";
    if (code >= 51) return "морось";
    if (code >= 3) return "облачно";
    return "ясно";
}

const char* WeekdayShort(int wday) {
    static const char* kD[] = {"ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ"};
    if (wday < 0 || wday > 6) return "";
    return kD[wday];
}

const char* MonthShort(int mon) {
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

lv_obj_t* MakeBtn(lv_obj_t* parent, int w, int h) {
    lv_obj_t* o = MakePlain(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, h / 2, 0);
    lv_obj_set_style_bg_color(o, kCard, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
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

void Track(lv_obj_t** list, int* n, int cap, lv_obj_t* o) {
    if (o && *n < cap) list[(*n)++] = o;
}

const lv_font_t* FaceFont(Theme* theme) {
    auto* t = static_cast<LvglTheme*>(theme);
    if (t && t->text_font()) return t->text_font()->font();
    return nullptr;
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
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

void ValeraLcdDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();
    DisplayLockGuard lock(this);
    lv_display_add_event_cb(display_, RounderEventCb, LV_EVENT_INVALIDATE_AREA, NULL);
    HideStockChrome();
    LoadClockStyle();
    BuildFace();
    face_ready_ = true;
    timer_ = lv_timer_create(TickThunk, 33, this);
    if (!boot_sound_played_) {
        boot_sound_played_ = true;
        Application::GetInstance().Schedule([]() {
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);
        });
    }
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
    std::string saved = s.GetString("act_code");
    if (saved.size() >= 4 && saved.size() <= 8) {
        std::strncpy(activation_code_, saved.c_str(), sizeof(activation_code_) - 1);
    }
    activation_bound_ = s.GetInt("act_bound", 0) != 0;
}

void ValeraLcdDisplay::SetClockStyle(int look, int scale_pct, uint32_t hh_color, uint32_t mm_color) {
    if (look < 0) look = 0;
    if (look > 4) look = 4;
    if (scale_pct < 80) scale_pct = 80;
    if (scale_pct > 160) scale_pct = 160;
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

void ValeraLcdDisplay::ColorSegDigit(SegDigit* d, lv_color_t color) {
    if (!d) return;
    for (int i = 0; i < 7; ++i) {
        if (d->seg[i]) lv_obj_set_style_bg_color(d->seg[i], color, 0);
    }
}

void ValeraLcdDisplay::BuildSegDigit(SegDigit* d, int cx, int cy, int dw, int dh, int thick) {
    if (!d) return;
    auto bar = [&](int x, int y, int w, int h) -> lv_obj_t* {
        lv_obj_t* o = MakePlain(root_);
        lv_obj_set_size(o, w, h);
        lv_obj_set_pos(o, x, y);
        lv_obj_set_style_radius(o, thick / 2, 0);
        lv_obj_set_style_bg_color(o, kWhite, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        Track(clock_objs_, &clock_obj_n_, 48, o);
        return o;
    };
    const int x0 = cx - dw / 2;
    const int y0 = cy - dh / 2;
    const int mid = y0 + dh / 2 - thick / 2;
    d->seg[0] = bar(x0 + thick, y0, dw - 2 * thick, thick);
    d->seg[1] = bar(x0 + dw - thick, y0 + thick, thick, dh / 2 - thick);
    d->seg[2] = bar(x0 + dw - thick, mid + thick, thick, dh / 2 - thick);
    d->seg[3] = bar(x0 + thick, y0 + dh - thick, dw - 2 * thick, thick);
    d->seg[4] = bar(x0, mid + thick, thick, dh / 2 - thick);
    d->seg[5] = bar(x0, y0 + thick, thick, dh / 2 - thick);
    d->seg[6] = bar(x0 + thick, mid, dw - 2 * thick, thick);
}

void ValeraLcdDisplay::SetSegDigit(SegDigit* d, int value, lv_color_t color) {
    if (!d) return;
    static const uint8_t kMask[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    if (value < 0 || value > 9) value = 0;
    const uint8_t m = kMask[value];
    for (int i = 0; i < 7; ++i) {
        if (!d->seg[i]) continue;
        const bool on = (m >> i) & 1;
        lv_obj_set_style_bg_color(d->seg[i], color, 0);
        lv_obj_set_style_bg_opa(d->seg[i], on ? LV_OPA_COVER : LV_OPA_10, 0);
    }
}

void ValeraLcdDisplay::ApplyClockStyleVisual() {
    lv_color_t hh = lv_color_hex(clock_hh_color_);
    lv_color_t mm = lv_color_hex(clock_mm_color_);
    if (clock_look_ == 1) {
        hh = kCyan;
        mm = kMagenta;
    } else if (clock_look_ == 2) {
        hh = lv_color_hex(0xFFE8EE);
        mm = kCoral;
    } else if (clock_look_ == 3) {
        hh = lv_color_hex(0xF5C400);
        mm = lv_color_hex(0xC45C26);
    } else if (clock_look_ == 4) {
        hh = lv_color_hex(0x7CFFB2);
        mm = lv_color_hex(0x5EEAD4);
    }
    ColorSegDigit(&clock_hh_[0], hh);
    ColorSegDigit(&clock_hh_[1], hh);
    ColorSegDigit(&clock_mm_[0], mm);
    ColorSegDigit(&clock_mm_[1], mm);
    for (int i = 0; i < 2; ++i) {
        if (clock_colon_[i]) lv_obj_set_style_bg_color(clock_colon_[i], mm, 0);
    }
    last_seg_hh_[0] = last_seg_hh_[1] = last_seg_mm_[0] = last_seg_mm_[1] = -1;
}

void ValeraLcdDisplay::BuildFace() {
    const int W = LV_HOR_RES;
    const int H = LV_VER_RES;
    const int min_side = W < H ? W : H;
    const int cx = W / 2;
    const int cy = H / 2;
    const lv_font_t* font = FaceFont(current_theme_);

    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, kBg, 0);

    root_ = MakePlain(screen);
    lv_obj_set_size(root_, W, H);
    lv_obj_set_style_bg_color(root_, kBg, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    bezel_ = MakePlain(root_);
    lv_obj_add_flag(bezel_, LV_OBJ_FLAG_HIDDEN);

    shade_grab_ = MakePlain(root_);
    lv_obj_set_size(shade_grab_, 140, 40);
    lv_obj_align(shade_grab_, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_add_flag(shade_grab_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(shade_grab_, LV_OPA_TRANSP, 0);
    lv_obj_t* grab_bar = MakePlain(shade_grab_);
    lv_obj_set_size(grab_bar, 56, 5);
    lv_obj_align(grab_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(grab_bar, 3, 0);
    lv_obj_set_style_bg_color(grab_bar, kEdge, 0);
    lv_obj_set_style_bg_opa(grab_bar, LV_OPA_70, 0);
    lv_obj_add_event_cb(
        shade_grab_,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<ValeraLcdDisplay*>(lv_event_get_user_data(e));
            if (self) self->OpenQuickShade();
        },
        LV_EVENT_CLICKED, this);

    // Halo-like horizontal HH:MM
    const float scale = (clock_scale_pct_ / 100.f) * 1.12f;
    const int dig_w = (int)(min_side * 0.155f * scale);
    const int dig_h = (int)(min_side * 0.34f * scale);
    const int dig_t = (int)(min_side * 0.030f * scale);
    const int gap_x = (int)(min_side * 0.016f * scale);
    const int colon_gap = (int)(min_side * 0.038f * scale);
    const int time_cy = cy - (int)(min_side * 0.02f);
    const int cell = dig_w + gap_x;
    const int hh1_cx = cx - colon_gap / 2 - cell / 2 - dig_w / 2;
    const int hh0_cx = hh1_cx - cell;
    const int mm0_cx = cx + colon_gap / 2 + cell / 2 + dig_w / 2;
    const int mm1_cx = mm0_cx + cell;
    BuildSegDigit(&clock_hh_[0], hh0_cx, time_cy, dig_w, dig_h, dig_t);
    BuildSegDigit(&clock_hh_[1], hh1_cx, time_cy, dig_w, dig_h, dig_t);
    BuildSegDigit(&clock_mm_[0], mm0_cx, time_cy, dig_w, dig_h, dig_t);
    BuildSegDigit(&clock_mm_[1], mm1_cx, time_cy, dig_w, dig_h, dig_t);
    for (int i = 0; i < 2; ++i) {
        clock_colon_[i] = MakePlain(root_);
        lv_obj_set_size(clock_colon_[i], dig_t, dig_t);
        lv_obj_set_style_radius(clock_colon_[i], dig_t / 2, 0);
        lv_obj_set_style_bg_color(clock_colon_[i], kCoral, 0);
        lv_obj_set_style_bg_opa(clock_colon_[i], LV_OPA_COVER, 0);
        const int cy_dot = time_cy + (i == 0 ? -dig_h / 5 : dig_h / 5) - dig_t / 2;
        lv_obj_set_pos(clock_colon_[i], cx - dig_t / 2, cy_dot);
        Track(clock_objs_, &clock_obj_n_, 48, clock_colon_[i]);
    }
    SetSegDigit(&clock_hh_[0], 0, kWhite);
    SetSegDigit(&clock_hh_[1], 0, kWhite);
    SetSegDigit(&clock_mm_[0], 0, kCoral);
    SetSegDigit(&clock_mm_[1], 0, kCoral);
    ApplyClockStyleVisual();

    clock_edge_date_ = lv_label_create(root_);
    lv_label_set_text(clock_edge_date_, "");
    lv_obj_set_style_text_color(clock_edge_date_, lv_color_hex(0xC2C3CB), 0);
    lv_obj_set_style_text_align(clock_edge_date_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(clock_edge_date_, font, 0);
    lv_obj_set_width(clock_edge_date_, (int)(min_side * 0.70f));
    lv_label_set_long_mode(clock_edge_date_, LV_LABEL_LONG_CLIP);
    lv_obj_align(clock_edge_date_, LV_ALIGN_TOP_MID, 0, (int)(min_side * 0.14f));
    Track(clock_objs_, &clock_obj_n_, 48, clock_edge_date_);

    // Activation code banner (bottom of clock) — always visible until bound.
    activation_banner_ = lv_label_create(root_);
    lv_label_set_text(activation_banner_, "");
    lv_obj_set_style_text_color(activation_banner_, kCyan, 0);
    lv_obj_set_style_text_align(activation_banner_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(activation_banner_, font, 0);
    lv_obj_set_width(activation_banner_, (int)(min_side * 0.78f));
    lv_label_set_long_mode(activation_banner_, LV_LABEL_LONG_CLIP);
    lv_obj_align(activation_banner_, LV_ALIGN_BOTTOM_MID, 0, -(int)(min_side * 0.14f));
    lv_obj_add_flag(activation_banner_, LV_OBJ_FLAG_HIDDEN);
    Track(clock_objs_, &clock_obj_n_, 48, activation_banner_);
    PaintActivationBanner();

    // Weather page
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
    if (font) lv_obj_set_style_text_font(weather_out_label_, font, 0);
    lv_obj_align(weather_out_label_, LV_ALIGN_CENTER, 0, -10);
    Track(weather_objs_, &weather_obj_n_, 16, weather_out_label_);

    weather_room_label_ = lv_label_create(root_);
    lv_label_set_text(weather_room_label_, "");
    lv_obj_set_style_text_color(weather_room_label_, kMagenta, 0);
    lv_obj_set_style_text_align(weather_room_label_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(weather_room_label_, font, 0);
    lv_obj_align(weather_room_label_, LV_ALIGN_CENTER, 0, 70);
    Track(weather_objs_, &weather_obj_n_, 16, weather_room_label_);

    weather_cond_label_ = lv_label_create(root_);
    lv_label_set_text(weather_cond_label_, "");
    lv_obj_set_style_text_color(weather_cond_label_, kEdge, 0);
    lv_obj_set_style_text_align(weather_cond_label_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(weather_cond_label_, font, 0);
    lv_obj_align(weather_cond_label_, LV_ALIGN_CENTER, 0, 110);
    Track(weather_objs_, &weather_obj_n_, 16, weather_cond_label_);

    // Assistant face (no orbs)
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

    voice_eq_cy_ = (int16_t)(min_side * 0.82f);
    BuildSpectrum(voice_eq_, reply_objs_, &reply_obj_n_, 40, voice_eq_cy_, kCoral);

    const int text_w = (int)(min_side * 0.68f);
    caption_label_ = lv_label_create(root_);
    lv_label_set_text(caption_label_, "");
    lv_obj_set_width(caption_label_, text_w);
    lv_label_set_long_mode(caption_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(caption_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(caption_label_, kWhite, 0);
    if (font) lv_obj_set_style_text_font(caption_label_, font, 0);
    lv_obj_align(caption_label_, LV_ALIGN_CENTER, 0, -20);
    Track(reply_objs_, &reply_obj_n_, 40, caption_label_);

    wifi_title_ = lv_label_create(root_);
    lv_label_set_text(wifi_title_, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_title_, kOrange, 0);
    lv_obj_set_style_text_align(wifi_title_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(wifi_title_, font, 0);
    lv_obj_align(wifi_title_, LV_ALIGN_TOP_MID, 0, (int)(min_side * 0.12f));
    Track(wifi_objs_, &wifi_obj_n_, 12, wifi_title_);

    wifi_ssid_ = lv_label_create(root_);
    lv_label_set_text(wifi_ssid_, "MyWiFi");
    lv_obj_set_width(wifi_ssid_, text_w);
    lv_label_set_long_mode(wifi_ssid_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(wifi_ssid_, kWhite, 0);
    lv_obj_set_style_text_align(wifi_ssid_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(wifi_ssid_, font, 0);
    lv_obj_align(wifi_ssid_, LV_ALIGN_CENTER, 0, -30);
    Track(wifi_objs_, &wifi_obj_n_, 12, wifi_ssid_);

    wifi_hint_ = lv_label_create(root_);
    lv_label_set_text(wifi_hint_, "Подключение…");
    lv_obj_set_width(wifi_hint_, text_w);
    lv_label_set_long_mode(wifi_hint_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(wifi_hint_, kEdge, 0);
    lv_obj_set_style_text_align(wifi_hint_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(wifi_hint_, font, 0);
    lv_obj_align(wifi_hint_, LV_ALIGN_CENTER, 0, 80);
    Track(wifi_objs_, &wifi_obj_n_, 12, wifi_hint_);

    BuildShade();
    BuildSettings();
    BuildRadio();
    BuildStations();

    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root_, GestureEventCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(root_, GestureEventCb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(root_, GestureEventCb, LV_EVENT_PRESSING, this);

    idle_step_ = 0;
    ShowIdlePage(IdlePage::kClock);
    SetGroupHidden(face_objs_, face_obj_n_, true);
    SetGroupHidden(reply_objs_, reply_obj_n_, true);
    SetGroupHidden(wifi_objs_, wifi_obj_n_, true);
    SetGroupHidden(radio_objs_, radio_obj_n_, true);
    SetGroupHidden(stations_objs_, stations_obj_n_, true);
    PickGazeTarget(act_eye_w_ * 0.18f, act_eye_h_ * 0.16f);
    next_blink_tick_ = 100 + (rand() % 120);
}


void ValeraLcdDisplay::ReloadActivationFromNvs() {
    Settings s("valera", false);
    activation_bound_ = s.GetInt("act_bound", 0) != 0;
    std::string saved = s.GetString("act_code");
    if (!saved.empty() && !activation_bound_) {
        std::strncpy(activation_code_, saved.c_str(), sizeof(activation_code_) - 1);
        activation_code_[sizeof(activation_code_) - 1] = '\0';
    }
}

void ValeraLcdDisplay::PaintActivationBanner() {
    if (!activation_banner_) return;
    if (!activation_bound_ && activation_code_[0] != '\0') {
        char buf[40];
        snprintf(buf, sizeof(buf), "КОД  %s", activation_code_);
        lv_label_set_text(activation_banner_, buf);
        lv_obj_clear_flag(activation_banner_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(activation_banner_, "");
        lv_obj_add_flag(activation_banner_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ValeraLcdDisplay::StationStep(int delta) {
    if (kValeraRadioStationCount <= 0) return;
    ValeraRadioPrefs::Load();
    if (delta != 0) {
        int idx = radio_station_idx_;
        for (int n = 0; n < kValeraRadioStationCount; ++n) {
            idx += delta;
            while (idx < 0) idx += kValeraRadioStationCount;
            while (idx >= kValeraRadioStationCount) idx -= kValeraRadioStationCount;
            if (!ValeraRadioPrefs::IsHidden(idx)) {
                radio_station_idx_ = idx;
                break;
            }
        }
    } else if (ValeraRadioPrefs::IsHidden(radio_station_idx_)) {
        for (int i = 0; i < kValeraRadioStationCount; ++i) {
            if (!ValeraRadioPrefs::IsHidden(i)) {
                radio_station_idx_ = i;
                break;
            }
        }
    }
    if (stations_name_ && radio_station_idx_ >= 0 && radio_station_idx_ < kValeraRadioStationCount) {
        const auto& st = kValeraRadioStations[radio_station_idx_];
        lv_label_set_text(stations_name_, st.name);
        if (stations_meta_) {
            char mbuf[48];
            snprintf(mbuf, sizeof(mbuf), "%s  %d kbps", st.genre, st.kbps);
            lv_label_set_text(stations_meta_, mbuf);
        }
    }
}

void ValeraLcdDisplay::BuildRadio() {
    const lv_font_t* font = FaceFont(current_theme_);
    const int min_side = LV_HOR_RES < LV_VER_RES ? LV_HOR_RES : LV_VER_RES;
    const int text_w = (int)(min_side * 0.72f);

    radio_title_ = lv_label_create(root_);
    lv_label_set_text(radio_title_, "Радио");
    lv_obj_set_style_text_color(radio_title_, kCyan, 0);
    lv_obj_set_style_text_align(radio_title_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(radio_title_, font, 0);
    lv_obj_align(radio_title_, LV_ALIGN_TOP_MID, 0, (int)(min_side * 0.12f));
    Track(radio_objs_, &radio_obj_n_, 40, radio_title_);

    radio_station_ = lv_label_create(root_);
    lv_label_set_text(radio_station_, "—");
    lv_obj_set_width(radio_station_, text_w);
    lv_label_set_long_mode(radio_station_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(radio_station_, kWhite, 0);
    lv_obj_set_style_text_align(radio_station_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(radio_station_, font, 0);
    lv_obj_align(radio_station_, LV_ALIGN_CENTER, 0, -50);
    Track(radio_objs_, &radio_obj_n_, 40, radio_station_);

    radio_meta_ = lv_label_create(root_);
    lv_label_set_text(radio_meta_, "");
    lv_obj_set_width(radio_meta_, text_w);
    lv_label_set_long_mode(radio_meta_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(radio_meta_, kEdge, 0);
    lv_obj_set_style_text_align(radio_meta_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(radio_meta_, font, 0);
    lv_obj_align(radio_meta_, LV_ALIGN_CENTER, 0, -10);
    Track(radio_objs_, &radio_obj_n_, 40, radio_meta_);

    radio_eq_cy_ = (int16_t)(min_side * 0.58f);
    BuildSpectrum(radio_eq_, radio_objs_, &radio_obj_n_, 40, radio_eq_cy_, kMagenta);

    radio_play_btn_ = MakeBtn(root_, (int)(min_side * 0.34f), 48);
    lv_obj_align(radio_play_btn_, LV_ALIGN_CENTER, -(int)(min_side * 0.20f), (int)(min_side * 0.28f));
    lv_obj_set_user_data(radio_play_btn_, (void*)(intptr_t)50);
    lv_obj_add_event_cb(radio_play_btn_, RadioBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* pl = lv_label_create(radio_play_btn_);
    lv_label_set_text(pl, "Эфир");
    lv_obj_set_style_text_color(pl, kWhite, 0);
    if (font) lv_obj_set_style_text_font(pl, font, 0);
    lv_obj_center(pl);
    Track(radio_objs_, &radio_obj_n_, 40, radio_play_btn_);

    radio_list_btn_ = MakeBtn(root_, (int)(min_side * 0.34f), 48);
    lv_obj_align(radio_list_btn_, LV_ALIGN_CENTER, (int)(min_side * 0.20f), (int)(min_side * 0.28f));
    lv_obj_set_user_data(radio_list_btn_, (void*)(intptr_t)51);
    lv_obj_add_event_cb(radio_list_btn_, RadioBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* ll = lv_label_create(radio_list_btn_);
    lv_label_set_text(ll, "Станции");
    lv_obj_set_style_text_color(ll, kWhite, 0);
    if (font) lv_obj_set_style_text_font(ll, font, 0);
    lv_obj_center(ll);
    Track(radio_objs_, &radio_obj_n_, 40, radio_list_btn_);
}

void ValeraLcdDisplay::BuildStations() {
    const lv_font_t* font = FaceFont(current_theme_);
    const int min_side = LV_HOR_RES < LV_VER_RES ? LV_HOR_RES : LV_VER_RES;
    const int text_w = (int)(min_side * 0.70f);

    stations_title_ = lv_label_create(root_);
    lv_label_set_text(stations_title_, "Станции");
    lv_obj_set_style_text_color(stations_title_, kWhite, 0);
    lv_obj_set_style_text_align(stations_title_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(stations_title_, font, 0);
    lv_obj_align(stations_title_, LV_ALIGN_TOP_MID, 0, (int)(min_side * 0.14f));
    Track(stations_objs_, &stations_obj_n_, 16, stations_title_);

    stations_name_ = lv_label_create(root_);
    lv_label_set_text(stations_name_, "—");
    lv_obj_set_width(stations_name_, text_w);
    lv_label_set_long_mode(stations_name_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(stations_name_, kCyan, 0);
    lv_obj_set_style_text_align(stations_name_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(stations_name_, font, 0);
    lv_obj_align(stations_name_, LV_ALIGN_CENTER, 0, -36);
    Track(stations_objs_, &stations_obj_n_, 16, stations_name_);

    stations_meta_ = lv_label_create(root_);
    lv_label_set_text(stations_meta_, "");
    lv_obj_set_width(stations_meta_, text_w);
    lv_label_set_long_mode(stations_meta_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(stations_meta_, kEdge, 0);
    lv_obj_set_style_text_align(stations_meta_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(stations_meta_, font, 0);
    lv_obj_align(stations_meta_, LV_ALIGN_CENTER, 0, 24);
    Track(stations_objs_, &stations_obj_n_, 16, stations_meta_);

    const int btn = 56;
    stations_prev_ = MakeBtn(root_, btn, btn);
    lv_obj_align(stations_prev_, LV_ALIGN_CENTER, -(int)(min_side * 0.32f), (int)(min_side * 0.22f));
    lv_obj_set_user_data(stations_prev_, (void*)(intptr_t)60);
    lv_obj_add_event_cb(stations_prev_, StationRowEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* pl = lv_label_create(stations_prev_);
    lv_label_set_text(pl, "<");
    lv_obj_set_style_text_color(pl, kWhite, 0);
    if (font) lv_obj_set_style_text_font(pl, font, 0);
    lv_obj_center(pl);
    Track(stations_objs_, &stations_obj_n_, 16, stations_prev_);

    stations_next_ = MakeBtn(root_, btn, btn);
    lv_obj_align(stations_next_, LV_ALIGN_CENTER, (int)(min_side * 0.32f), (int)(min_side * 0.22f));
    lv_obj_set_user_data(stations_next_, (void*)(intptr_t)61);
    lv_obj_add_event_cb(stations_next_, StationRowEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* nl = lv_label_create(stations_next_);
    lv_label_set_text(nl, ">");
    lv_obj_set_style_text_color(nl, kWhite, 0);
    if (font) lv_obj_set_style_text_font(nl, font, 0);
    lv_obj_center(nl);
    Track(stations_objs_, &stations_obj_n_, 16, stations_next_);

    stations_pick_ = MakeBtn(root_, (int)(min_side * 0.40f), 52);
    lv_obj_align(stations_pick_, LV_ALIGN_CENTER, 0, (int)(min_side * 0.22f));
    lv_obj_set_user_data(stations_pick_, (void*)(intptr_t)62);
    lv_obj_add_event_cb(stations_pick_, StationRowEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* pk = lv_label_create(stations_pick_);
    lv_label_set_text(pk, "Выбрать");
    lv_obj_set_style_text_color(pk, kWhite, 0);
    if (font) lv_obj_set_style_text_font(pk, font, 0);
    lv_obj_center(pk);
    Track(stations_objs_, &stations_obj_n_, 16, stations_pick_);
}

void ValeraLcdDisplay::BuildShade() {
    const lv_font_t* font = FaceFont(current_theme_);
    const int row_w = (int)(LV_HOR_RES * 0.72f);
    const int btn = 48;

    shade_panel_h_ = (int)(LV_VER_RES * 0.58f);
    shade_panel_ = MakePlain(root_);
    lv_obj_set_size(shade_panel_, LV_HOR_RES, shade_panel_h_);
    lv_obj_set_style_bg_color(shade_panel_, lv_color_hex(0x12161C), 0);
    lv_obj_set_style_bg_opa(shade_panel_, LV_OPA_COVER, 0);
    lv_obj_add_flag(shade_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(shade_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(shade_panel_, GestureEventCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(shade_panel_, GestureEventCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(shade_panel_, GestureEventCb, LV_EVENT_RELEASED, this);

    shade_handle_ = MakePlain(shade_panel_);
    lv_obj_set_size(shade_handle_, 56, 5);
    lv_obj_align(shade_handle_, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_radius(shade_handle_, 3, 0);
    lv_obj_set_style_bg_color(shade_handle_, lv_color_hex(0x6a7a90), 0);
    lv_obj_set_style_bg_opa(shade_handle_, LV_OPA_COVER, 0);

    shade_title_ = lv_label_create(shade_panel_);
    lv_label_set_text(shade_title_, "Управление");
    lv_obj_set_style_text_color(shade_title_, kWhite, 0);
    if (font) lv_obj_set_style_text_font(shade_title_, font, 0);
    lv_obj_align(shade_title_, LV_ALIGN_TOP_MID, 0, 28);

    auto make_pm = [&](lv_obj_t** minus, lv_obj_t** plus, lv_obj_t** val, const char* title, int y,
                       int id_base) {
        lv_obj_t* row = MakePlain(shade_panel_);
        lv_obj_set_size(row, row_w, btn);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);

        lv_obj_t* lab = lv_label_create(row);
        lv_label_set_text(lab, title);
        lv_obj_set_style_text_color(lab, kEdge, 0);
        if (font) lv_obj_set_style_text_font(lab, font, 0);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);

        *minus = MakeBtn(row, btn, btn);
        lv_obj_align(*minus, LV_ALIGN_RIGHT_MID, -(btn * 2 + 10), 0);
        lv_obj_set_user_data(*minus, (void*)(intptr_t)id_base);
        lv_obj_add_event_cb(*minus, ShadeBtnEventCb, LV_EVENT_CLICKED, this);
        lv_obj_t* ml = lv_label_create(*minus);
        lv_label_set_text(ml, "-");
        lv_obj_set_style_text_color(ml, kWhite, 0);
        if (font) lv_obj_set_style_text_font(ml, font, 0);
        lv_obj_center(ml);

        *val = lv_label_create(row);
        lv_label_set_text(*val, "--");
        lv_obj_set_style_text_color(*val, kWhite, 0);
        if (font) lv_obj_set_style_text_font(*val, font, 0);
        lv_obj_align(*val, LV_ALIGN_RIGHT_MID, -(btn + 14), 0);

        *plus = MakeBtn(row, btn, btn);
        lv_obj_align(*plus, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_user_data(*plus, (void*)(intptr_t)(id_base + 1));
        lv_obj_add_event_cb(*plus, ShadeBtnEventCb, LV_EVENT_CLICKED, this);
        lv_obj_t* pl = lv_label_create(*plus);
        lv_label_set_text(pl, "+");
        lv_obj_set_style_text_color(pl, kWhite, 0);
        if (font) lv_obj_set_style_text_font(pl, font, 0);
        lv_obj_center(pl);
    };

    make_pm(&shade_bri_minus_, &shade_bri_plus_, &shade_bri_val_, "Яркость", 70, 10);
    make_pm(&shade_vol_minus_, &shade_vol_plus_, &shade_vol_val_, "Громкость", 70 + 58, 20);

    shade_settings_btn_ = MakeBtn(shade_panel_, row_w, btn);
    lv_obj_align(shade_settings_btn_, LV_ALIGN_TOP_MID, 0, 70 + 58 * 2);
    lv_obj_set_user_data(shade_settings_btn_, (void*)(intptr_t)30);
    lv_obj_add_event_cb(shade_settings_btn_, ShadeBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* sl = lv_label_create(shade_settings_btn_);
    lv_label_set_text(sl, "Настройки / код");
    lv_obj_set_style_text_color(sl, kCyan, 0);
    if (font) lv_obj_set_style_text_font(sl, font, 0);
    lv_obj_center(sl);

    // Keep pointers null — theme/wifi/radio removed from shade to cut clutter & reboot traps.
    shade_radio_btn_ = nullptr;
    shade_theme_btn_ = nullptr;
    shade_wifi_btn_ = nullptr;

    SetShadePull(0.f);
}

void ValeraLcdDisplay::BuildSettings() {
    const lv_font_t* font = FaceFont(current_theme_);
    settings_panel_ = MakePlain(root_);
    lv_obj_set_size(settings_panel_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(settings_panel_, 0, 0);
    lv_obj_set_style_bg_color(settings_panel_, lv_color_hex(0x070A0F), 0);
    lv_obj_set_style_bg_opa(settings_panel_, LV_OPA_COVER, 0);
    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);

    settings_title_ = lv_label_create(settings_panel_);
    lv_label_set_text(settings_title_, "Код активации");
    lv_obj_set_style_text_color(settings_title_, kEdge, 0);
    if (font) lv_obj_set_style_text_font(settings_title_, font, 0);
    lv_obj_align(settings_title_, LV_ALIGN_TOP_MID, 0, (int)(LV_VER_RES * 0.16f));

    settings_code_ = lv_label_create(settings_panel_);
    lv_label_set_text(settings_code_, "----");
    lv_obj_set_style_text_color(settings_code_, kCyan, 0);
    lv_obj_set_style_text_align(settings_code_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(settings_code_, font, 0);
    lv_obj_set_width(settings_code_, (int)(LV_HOR_RES * 0.80f));
    lv_label_set_long_mode(settings_code_, LV_LABEL_LONG_WRAP);
    lv_obj_align(settings_code_, LV_ALIGN_CENTER, 0, -30);

    settings_wifi_ = lv_label_create(settings_panel_);
    lv_label_set_text(settings_wifi_, "Wi-Fi: MyWiFi");
    lv_obj_set_style_text_color(settings_wifi_, kEdge, 0);
    lv_obj_set_style_text_align(settings_wifi_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(settings_wifi_, font, 0);
    lv_obj_set_width(settings_wifi_, (int)(LV_HOR_RES * 0.72f));
    lv_label_set_long_mode(settings_wifi_, LV_LABEL_LONG_WRAP);
    lv_obj_align(settings_wifi_, LV_ALIGN_CENTER, 0, 50);

    settings_hint_ = lv_label_create(settings_panel_);
    lv_label_set_text(settings_hint_, "xiaozhi.me");
    lv_obj_set_style_text_color(settings_hint_, kMagenta, 0);
    lv_obj_set_style_text_align(settings_hint_, LV_TEXT_ALIGN_CENTER, 0);
    if (font) lv_obj_set_style_text_font(settings_hint_, font, 0);
    lv_obj_set_width(settings_hint_, (int)(LV_HOR_RES * 0.70f));
    lv_label_set_long_mode(settings_hint_, LV_LABEL_LONG_WRAP);
    lv_obj_align(settings_hint_, LV_ALIGN_CENTER, 0, 90);

    settings_radio_btn_ = MakeBtn(settings_panel_, (int)(LV_HOR_RES * 0.55f), 48);
    lv_obj_align(settings_radio_btn_, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_set_user_data(settings_radio_btn_, (void*)(intptr_t)41);
    lv_obj_add_event_cb(settings_radio_btn_, SettingsBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* rl = lv_label_create(settings_radio_btn_);
    lv_label_set_text(rl, "Станции");
    lv_obj_set_style_text_color(rl, kWhite, 0);
    if (font) lv_obj_set_style_text_font(rl, font, 0);
    lv_obj_center(rl);

    settings_back_ = MakeBtn(settings_panel_, (int)(LV_HOR_RES * 0.50f), 48);
    lv_obj_align(settings_back_, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_user_data(settings_back_, (void*)(intptr_t)40);
    lv_obj_add_event_cb(settings_back_, SettingsBtnEventCb, LV_EVENT_CLICKED, this);
    lv_obj_t* bl = lv_label_create(settings_back_);
    lv_label_set_text(bl, "Назад");
    lv_obj_set_style_text_color(bl, kWhite, 0);
    if (font) lv_obj_set_style_text_font(bl, font, 0);
    lv_obj_center(bl);
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

void ValeraLcdDisplay::SetShadePull(float pull) {
    pull = Clampf(pull, 0.f, 1.f);
    shade_pull_ = pull;
    shade_open_ = pull > 0.02f;
    if (!shade_panel_) return;
    const float eased = Smoothstep(pull);
    lv_obj_set_y(shade_panel_, (int)(-shade_panel_h_ + eased * shade_panel_h_));
    if (pull <= 0.001f) {
        lv_obj_add_flag(shade_panel_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(shade_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(shade_panel_);
    }
}

void ValeraLcdDisplay::ShowShade(bool open) {
    if (settings_open_) return;
    shade_dragging_ = false;
    shade_target_ = open ? 1.f : 0.f;
    if (open) {
        RefreshShade();
        if (shade_pull_ < 0.05f) SetShadePull(0.12f);
    }
}

void ValeraLcdDisplay::ShowSettings(bool open) {
    settings_open_ = open;
    if (!settings_panel_) return;
    if (open) {
        ShowShade(false);
        ReloadActivationFromNvs();
        RefreshSettings();
        lv_obj_clear_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_panel_);
    } else {
        lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ValeraLcdDisplay::AnimateShadeTick() {
    if (shade_dragging_ || settings_open_) return;
    const float diff = shade_target_ - shade_pull_;
    if (fabsf(diff) < 0.004f) {
        if (shade_pull_ != shade_target_) SetShadePull(shade_target_);
        return;
    }
    SetShadePull(shade_pull_ + diff * 0.28f);
}

void ValeraLcdDisplay::NudgeBrightness(int delta) {
    if (auto* bl = Board::GetInstance().GetBacklight()) {
        int b = bl->brightness() + delta;
        if (b > 100) b = 100;
        if (b < 5) b = 5;
        bl->SetBrightness((uint8_t)b, true);
    }
    RefreshShade();
}

void ValeraLcdDisplay::NudgeVolume(int delta) {
    if (auto* codec = Board::GetInstance().GetAudioCodec()) {
        int v = codec->output_volume() + delta;
        if (v > 100) v = 100;
        if (v < 0) v = 0;
        codec->SetOutputVolume(v);
    }
    RefreshShade();
}

void ValeraLcdDisplay::RefreshShade() {
    char buf[16];
    int bright = 75;
    if (auto* bl = Board::GetInstance().GetBacklight()) bright = bl->brightness();
    int vol = 0;
    if (auto* codec = Board::GetInstance().GetAudioCodec()) vol = codec->output_volume();
    if (shade_bri_val_) {
        snprintf(buf, sizeof(buf), "%d%%", bright);
        lv_label_set_text(shade_bri_val_, buf);
    }
    if (shade_vol_val_) {
        snprintf(buf, sizeof(buf), "%d%%", vol);
        lv_label_set_text(shade_vol_val_, buf);
    }
}

void ValeraLcdDisplay::RefreshSettings() {
    ReloadActivationFromNvs();
    if (settings_code_) {
        if (!activation_bound_ && activation_code_[0] != '\0') {
            char buf[48];
            snprintf(buf, sizeof(buf), "%s", activation_code_);
            lv_label_set_text(settings_code_, buf);
        } else if (activation_bound_) {
            lv_label_set_text(settings_code_, "OK");
            if (settings_hint_) lv_label_set_text(settings_hint_, "Уже привязано");
        } else {
            lv_label_set_text(settings_code_, "....");
            if (settings_hint_) lv_label_set_text(settings_hint_, "Ждём код с сервера");
        }
    }
    if (settings_wifi_) {
        lv_label_set_text(settings_wifi_, "Wi-Fi: MyWiFi");
    }
    PaintActivationBanner();
}

void ValeraLcdDisplay::CycleTheme() {
    SetClockStyle((clock_look_ + 1) % 5, clock_scale_pct_, clock_hh_color_, clock_mm_color_);
}

void ValeraLcdDisplay::ShadeBtnEventCb(lv_event_t* e) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_event_get_user_data(e));
    if (self) self->OnShadeBtn(e);
}

void ValeraLcdDisplay::OnShadeBtn(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (shade_pull_ < 0.5f) return;
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    const int id = (int)(intptr_t)lv_obj_get_user_data(t);
    if (id == 10) NudgeBrightness(-10);
    else if (id == 11) NudgeBrightness(+10);
    else if (id == 20) NudgeVolume(-10);
    else if (id == 21) NudgeVolume(+10);
    else if (id == 30) ShowSettings(true);
}

void ValeraLcdDisplay::SettingsBtnEventCb(lv_event_t* e) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_event_get_user_data(e));
    if (self) self->OnSettingsBtn(e);
}

void ValeraLcdDisplay::OnSettingsBtn(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    const int id = (int)(intptr_t)lv_obj_get_user_data(t);
    if (id == 41) {
        ShowSettings(false);
        OpenStations();
        return;
    }
    ShowSettings(false);
    ShowShade(true);
}

void ValeraLcdDisplay::RadioBtnEventCb(lv_event_t* e) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_event_get_user_data(e));
    if (self) self->OnRadioBtn(e);
}

void ValeraLcdDisplay::OnRadioBtn(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    const int id = (int)(intptr_t)lv_obj_get_user_data(t);
    if (id == 50) {
        radio_playing_ = !radio_playing_;
        PaintRadio();
    } else if (id == 51) {
        OpenStations();
    }
}

void ValeraLcdDisplay::StationRowEventCb(lv_event_t* e) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_event_get_user_data(e));
    if (self) self->OnStationRow(e);
}

void ValeraLcdDisplay::OnStationRow(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* t = (lv_obj_t*)lv_event_get_target(e);
    const int id = (int)(intptr_t)lv_obj_get_user_data(t);
    if (id == 60) StationStep(-1);
    else if (id == 61) StationStep(+1);
    else if (id == 62) SelectStation(radio_station_idx_);
}

void ValeraLcdDisplay::OpenRadio() {
    idle_step_ = 2;
    ShowIdlePage(IdlePage::kRadio);
}

void ValeraLcdDisplay::OpenStations() {
    idle_step_ = 3;
    ShowIdlePage(IdlePage::kStations);
}

void ValeraLcdDisplay::SelectStation(int index) {
    if (index < 0 || index >= kValeraRadioStationCount) return;
    if (ValeraRadioPrefs::IsHidden(index)) return;
    radio_station_idx_ = index;
    radio_playing_ = true;
    OpenRadio();
    PaintRadio();
}

void ValeraLcdDisplay::PaintRadio() {
    if (radio_station_idx_ < 0 || radio_station_idx_ >= kValeraRadioStationCount) {
        radio_station_idx_ = 0;
    }
    const auto& st = kValeraRadioStations[radio_station_idx_];
    if (radio_station_) lv_label_set_text(radio_station_, st.name);
    if (radio_meta_) {
        char buf[80];
        if (radio_playing_) {
            snprintf(buf, sizeof(buf), "%s · %d kbps\nэфир выбран (стрим — след. шаг)", st.genre,
                     st.kbps);
        } else {
            snprintf(buf, sizeof(buf), "%s · %d kbps\nсвайп ← → · Станции", st.genre, st.kbps);
        }
        lv_label_set_text(radio_meta_, buf);
    }
    if (radio_play_btn_) {
        lv_obj_t* lab = lv_obj_get_child(radio_play_btn_, 0);
        if (lab) lv_label_set_text(lab, radio_playing_ ? "■  Стоп" : "▶  Эфир");
    }
}

void ValeraLcdDisplay::GestureEventCb(lv_event_t* e) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_event_get_user_data(e));
    if (self) self->OnGesture(e);
}

void ValeraLcdDisplay::OnGesture(lv_event_t* e) {
    if (settings_open_) return;
    if (shade_pull_ > 0.2f) {
        // Close shade with upward fling from root only when not on a button.
        const lv_event_code_t code = lv_event_get_code(e);
        lv_indev_t* indev = lv_indev_active();
        if (!indev) return;
        lv_point_t p{};
        lv_indev_get_point(indev, &p);
        if (code == LV_EVENT_PRESSED) {
            gesture_tracking_ = true;
            gesture_start_x_ = p.x;
            gesture_start_y_ = p.y;
            shade_dragging_ = false;
            return;
        }
        if (!gesture_tracking_) return;
        if (code == LV_EVENT_PRESSING) {
            const int dy = p.y - gesture_start_y_;
            if (dy < -8) {
                shade_dragging_ = true;
                SetShadePull(1.f + (float)dy / (float)shade_panel_h_);
            }
            return;
        }
        if (code == LV_EVENT_RELEASED) {
            gesture_tracking_ = false;
            if (shade_dragging_) {
                shade_dragging_ = false;
                shade_target_ = (shade_pull_ < 0.55f) ? 0.f : 1.f;
            }
        }
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p{};
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        gesture_tracking_ = true;
        shade_dragging_ = false;
        gesture_start_x_ = p.x;
        gesture_start_y_ = p.y;
        gesture_from_top_ = (p.y < 120);
        return;
    }
    if (!gesture_tracking_) return;

    if (code == LV_EVENT_PRESSING) {
        if (assistant_active_) return;
        const int dy = p.y - gesture_start_y_;
        if (gesture_from_top_ && dy > 10) {
            shade_dragging_ = true;
            if (shade_pull_ < 0.05f) RefreshShade();
            SetShadePull((float)dy / (float)shade_panel_h_);
        }
        return;
    }

    if (code != LV_EVENT_RELEASED) return;
    gesture_tracking_ = false;
    const int dx = p.x - gesture_start_x_;
    const int dy = p.y - gesture_start_y_;
    const int adx = dx < 0 ? -dx : dx;
    const int ady = dy < 0 ? -dy : dy;

    if (shade_dragging_) {
        shade_dragging_ = false;
        shade_target_ = (shade_pull_ > 0.40f || dy > 80) ? 1.f : 0.f;
        if (shade_target_ > 0.5f) RefreshShade();
        return;
    }
    if (assistant_active_) return;
    if (gesture_start_y_ < 140 && dy > 40 && ady > adx) {
        ShowShade(true);
        return;
    }
    if (adx > 56 && adx > ady * 1.2f) {
        if (dx < 0) AdvanceIdlePage();
        else RetreatIdlePage();
    }
}

bool ValeraLcdDisplay::InWifiStage() const { return active_mode_ == ActiveMode::kWifi; }

bool ValeraLcdDisplay::InReplyStage() const {
    return assistant_active_ && active_mode_ == ActiveMode::kSpeak && !hide_subtitle_ &&
           !InWifiStage();
}

void ValeraLcdDisplay::ShowIdlePage(IdlePage page) {
    idle_page_ = page;
    idle_page_ticks_ = 0;
    SetGroupHidden(clock_objs_, clock_obj_n_, page != IdlePage::kClock);
    SetGroupHidden(weather_objs_, weather_obj_n_, page != IdlePage::kWeather);
    SetGroupHidden(radio_objs_, radio_obj_n_, page != IdlePage::kRadio);
    SetGroupHidden(stations_objs_, stations_obj_n_, page != IdlePage::kStations);
    if (page == IdlePage::kWeather) {
        last_out_temp_ = 9999;
        last_room_temp_ = 9999;
        last_weather_code_ = -999;
        PaintWeather();
    }
    if (page == IdlePage::kClock) {
        last_hour_ = -1;
        last_min_ = -1;
        PaintActivationBanner();
    }
    if (page == IdlePage::kRadio) PaintRadio();
    if (page == IdlePage::kStations) {
        StationStep(0);  // refresh card for current index
    }
}

void ValeraLcdDisplay::AdvanceIdlePage() {
    static const IdlePage kSeq[] = {IdlePage::kClock, IdlePage::kWeather, IdlePage::kRadio,
                                    IdlePage::kStations};
    idle_step_ = (idle_step_ + 1) % 4;
    ShowIdlePage(kSeq[idle_step_]);
}

void ValeraLcdDisplay::RetreatIdlePage() {
    static const IdlePage kSeq[] = {IdlePage::kClock, IdlePage::kWeather, IdlePage::kRadio,
                                    IdlePage::kStations};
    idle_step_ = (idle_step_ + 3) % 4;
    ShowIdlePage(kSeq[idle_step_]);
}

void ValeraLcdDisplay::LayoutForActive(bool active) {
    assistant_active_ = active;
    if (active) {
        ShowSettings(false);
        shade_target_ = 0.f;
        SetShadePull(0.f);
        SetGroupHidden(clock_objs_, clock_obj_n_, true);
        SetGroupHidden(weather_objs_, weather_obj_n_, true);
        SetGroupHidden(radio_objs_, radio_obj_n_, true);
        SetGroupHidden(stations_objs_, stations_obj_n_, true);
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
    char line[96];
    char hint[220];
    if (wifi.IsConfigMode()) {
        if (wifi_title_) lv_label_set_text(wifi_title_, "Настройка Wi‑Fi");
        snprintf(line, sizeof(line), "%s", wifi.GetApSsid().c_str());
        if (wifi_ssid_) lv_label_set_text(wifi_ssid_, line);
        snprintf(hint, sizeof(hint),
                 "1) Телефон → Wi‑Fi → %s\n2) Браузер → %s\nТолько 2.4 ГГц", line,
                 wifi.GetApWebUrl().c_str());
        if (wifi_hint_) lv_label_set_text(wifi_hint_, hint);
        return;
    }
    if (wifi_title_) lv_label_set_text(wifi_title_, "Wi‑Fi");
    if (wifi.IsConnected()) {
        std::string ssid = wifi.GetSsid();
        if (ssid.empty()) ssid = "MyWiFi";
        if (wifi_ssid_) lv_label_set_text(wifi_ssid_, ssid.c_str());
        if (wifi_hint_) lv_label_set_text(wifi_hint_, "Подключено");
    } else {
        if (wifi_ssid_) lv_label_set_text(wifi_ssid_, "MyWiFi");
        if (wifi_hint_) lv_label_set_text(wifi_hint_, "Подключение…\nпароль 23456789");
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
    PickGazeTarget(0.18f * act_eye_w_, 0.16f * act_eye_h_);
}

void ValeraLcdDisplay::AnimateAmbient() {
    const float breath = 0.5f + 0.5f * sinf(tick_ * 0.025f);
    lv_color_t rim = kCyanDim;
    lv_opa_t rim_opa = (lv_opa_t)(24 + (int)(40 * breath));
    if (assistant_active_) {
        if (InWifiStage()) {
            rim = kOrange;
            rim_opa = (lv_opa_t)(40 + (int)(40 * breath));
        } else if (InReplyStage()) {
            rim = kMagentaDim;
            rim_opa = (lv_opa_t)(12 + (int)(20 * breath));
        } else if (active_mode_ == ActiveMode::kListen)
            rim = kCyan;
        else if (active_mode_ == ActiveMode::kThink)
            rim = kThink;
    }
    if (bezel_) {
        lv_obj_set_style_border_color(bezel_, rim, 0);
        lv_obj_set_style_border_opa(bezel_, rim_opa, 0);
    }
}

void ValeraLcdDisplay::PickGazeTarget(float max_x, float max_y) {
    target_x_ = ((float)rand() / (float)RAND_MAX * 2.f - 1.f) * max_x;
    target_y_ = ((float)rand() / (float)RAND_MAX * 2.f - 1.f) * max_y;
    next_gaze_tick_ = tick_ + 40 + (rand() % 90);
}

void ValeraLcdDisplay::PlacePupil(Eye* eye, int16_t w, int16_t h, int16_t pupil_r, float look_x,
                                  float look_y, float open) {
    if (!eye || !eye->body || !eye->pupil) return;
    const float oh = (float)h * Clampf(open, 0.12f, 1.f);
    lv_obj_set_height(eye->body, (int)oh);
    lv_obj_set_y(eye->body, eye->home_cy - (int)oh / 2);
    float px = (float)w * 0.5f + look_x;
    float py = oh * 0.5f + look_y;
    px = Clampf(px, pupil_r + 2.f, w - pupil_r - 2.f);
    py = Clampf(py, pupil_r + 2.f, oh - pupil_r - 2.f);
    lv_obj_set_pos(eye->pupil, (int)px - pupil_r, (int)py - pupil_r);
    if (eye->shine) lv_obj_set_pos(eye->shine, (int)px - pupil_r - 3, (int)py - pupil_r - 3);
}

void ValeraLcdDisplay::AnimateEyes(Eye* left, Eye* right, int16_t w, int16_t h, int16_t pupil_r,
                                   bool playful) {
    if (tick_ >= next_gaze_tick_) PickGazeTarget(w * (playful ? 0.22f : 0.18f), h * 0.16f);
    look_x_ += (target_x_ - look_x_) * 0.12f;
    look_y_ += (target_y_ - look_y_) * 0.12f;

    if (tick_ >= next_blink_tick_ && blink_phase_ == 0) {
        blink_phase_ = 1;
        blink_tick_ = 0;
    }
    float open = 1.f;
    if (blink_phase_ == 1) {
        blink_tick_++;
        if (blink_tick_ < 4) open = 1.f - blink_tick_ / 4.f;
        else if (blink_tick_ < 8) open = (blink_tick_ - 4) / 4.f;
        else {
            blink_phase_ = 0;
            next_blink_tick_ = tick_ + 80 + (rand() % 140);
        }
    }
    PlacePupil(left, w, h, pupil_r, look_x_, look_y_, open);
    PlacePupil(right, w, h, pupil_r, look_x_, look_y_, open);
}

void ValeraLcdDisplay::AnimateSpectrum(lv_obj_t* const* bars, int cy, float drive, float speed,
                                       lv_color_t c0, lv_color_t c1) {
    const int mid = kSpectrumN / 2;
    constexpr float kHalfPi = 1.5707963f;
    for (int i = 0; i < kSpectrumN; ++i) {
        if (!bars[i]) continue;
        const int mirror = (i <= mid) ? i : (kSpectrumN - 1 - i);
        const float shape = 0.22f + 0.78f * sinf((mirror + 0.5f) / (mid + 1.f) * kHalfPi);
        const float wobble =
            0.45f + 0.55f * fabsf(sinf(tick_ * speed + mirror * 0.70f + 0.15f * sinf(tick_ * 0.07f)));
        const int h = (int)(16 + 70 * drive * shape * wobble);
        lv_obj_set_height(bars[i], h);
        lv_obj_set_y(bars[i], cy - h / 2);
        lv_obj_set_style_bg_color(bars[i], (mirror % 2) ? c1 : c0, 0);
    }
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
        case kDeviceStateStarting:
        case kDeviceStateActivating:
            want = ActiveMode::kIdle;
            break;
        case kDeviceStateConnecting:
            want = ActiveMode::kThink;
            break;
        case kDeviceStateWifiConfiguring: {
            auto& wifi = WifiManager::GetInstance();
            want = wifi.IsConnected() ? ActiveMode::kIdle : ActiveMode::kWifi;
            break;
        }
        default:
            want = ActiveMode::kThink;
            break;
    }
    if (want != active_mode_) SetActiveMode(want);

    AnimateShadeTick();
    if (settings_open_) {
        if ((tick_ % 60) == 0) RefreshSettings();
        return;
    }
    if (shade_pull_ > 0.05f || shade_dragging_) return;

    AnimateAmbient();

    if (assistant_active_) {
        if (InWifiStage()) {
            if ((tick_ % 60) == 0) PaintWifiHint();
            return;
        }
        if (InReplyStage()) {
            AnimateSpectrum(voice_eq_, voice_eq_cy_, 0.95f, 0.40f, kCoral, kMagenta);
        } else {
            AnimateEyes(&act_left_, &act_right_, act_eye_w_, act_eye_h_, act_pupil_r_, false);
            if (active_mode_ == ActiveMode::kListen)
                AnimateSpectrum(spectrum_, spectrum_center_y_, 0.90f, 0.34f, kCyan, kCyan);
            else if (active_mode_ == ActiveMode::kThink)
                AnimateSpectrum(spectrum_, spectrum_center_y_, 0.40f, 0.14f, kThink, kMagenta);
            else
                AnimateSpectrum(spectrum_, spectrum_center_y_, 0.70f, 0.28f, kMagenta, kCoral);
        }
        return;
    }

    if (idle_page_ == IdlePage::kClock) {
        // Wall clock comes from SNTP / Open-Meteo via settimeofday (see ValeraSensors).
        time_t now = time(nullptr);
        struct tm tm_info = {};
        localtime_r(&now, &tm_info);
        const bool blink = ((tick_ / 30) % 2) == 0;
        if (tm_info.tm_hour != last_hour_ || tm_info.tm_min != last_min_ ||
            tm_info.tm_wday != last_wday_ || tm_info.tm_mday != last_mday_ ||
            (tick_ % 30) == 0) {
            PaintClock(&tm_info, blink);
            last_hour_ = tm_info.tm_hour;
            last_min_ = tm_info.tm_min;
            last_wday_ = tm_info.tm_wday;
            last_mday_ = tm_info.tm_mday;
        }
    } else if (idle_page_ == IdlePage::kWeather) {
        if ((tick_ % 60) == 0) PaintWeather();
    } else if (idle_page_ == IdlePage::kRadio) {
        const float drive = radio_playing_ ? 0.95f : 0.25f;
        AnimateSpectrum(radio_eq_, radio_eq_cy_, drive, 0.36f, kMagenta, kCyan);
    }
}

void ValeraLcdDisplay::PaintClock(const struct tm* tm, bool colon_on) {
    if (!tm) return;
    lv_color_t hh_c = lv_color_hex(clock_hh_color_);
    lv_color_t mm_c = lv_color_hex(clock_mm_color_);
    if (clock_look_ == 1) {
        hh_c = kCyan;
        mm_c = kMagenta;
    } else if (clock_look_ == 2) {
        hh_c = lv_color_hex(0xFFE8EE);
        mm_c = kCoral;
    } else if (clock_look_ == 3) {
        hh_c = lv_color_hex(0xF5C400);
        mm_c = lv_color_hex(0xC45C26);
    } else if (clock_look_ == 4) {
        hh_c = lv_color_hex(0x7CFFB2);
        mm_c = lv_color_hex(0x5EEAD4);
    }
    const int h0 = tm->tm_hour / 10, h1 = tm->tm_hour % 10;
    const int m0 = tm->tm_min / 10, m1 = tm->tm_min % 10;
    if (h0 != last_seg_hh_[0]) {
        SetSegDigit(&clock_hh_[0], h0, hh_c);
        last_seg_hh_[0] = h0;
    }
    if (h1 != last_seg_hh_[1]) {
        SetSegDigit(&clock_hh_[1], h1, hh_c);
        last_seg_hh_[1] = h1;
    }
    if (m0 != last_seg_mm_[0]) {
        SetSegDigit(&clock_mm_[0], m0, mm_c);
        last_seg_mm_[0] = m0;
    }
    if (m1 != last_seg_mm_[1]) {
        SetSegDigit(&clock_mm_[1], m1, mm_c);
        last_seg_mm_[1] = m1;
    }
    const lv_opa_t colon_opa = colon_on ? LV_OPA_COVER : LV_OPA_30;
    for (int i = 0; i < 2; ++i) {
        if (clock_colon_[i]) {
            lv_obj_set_style_bg_color(clock_colon_[i], mm_c, 0);
            lv_obj_set_style_bg_opa(clock_colon_[i], colon_opa, 0);
        }
    }

    ValeraClimate c = ValeraSensors::Get();
    if (false && clock_edge_weather_) {
        if (c.outdoor_ok) {
            char weather_line[32];
            snprintf(weather_line, sizeof(weather_line), "%d° · %s", (int)lroundf(c.outdoor_c),
                     WeatherWord(c.weather_code));
            lv_label_set_text(clock_edge_weather_, weather_line);
        } else {
            lv_label_set_text(clock_edge_weather_, "—°");
        }
    }
    if (clock_edge_date_) {
        const bool time_ok = c.time_ok || (tm->tm_year + 1900) >= 2024;
        if (!time_ok) {
            lv_label_set_text(clock_edge_date_, "синхр. времени…");
        } else {
            char date_line[24];
            snprintf(date_line, sizeof(date_line), "%s  %d  %s", WeekdayShort(tm->tm_wday),
                     tm->tm_mday, MonthShort(tm->tm_mon));
            lv_label_set_text(clock_edge_date_, date_line);
        }
    }
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
        if (weather_glyph_) lv_obj_set_style_bg_color(weather_glyph_, g, 0);
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
    DisplayLockGuard lock(this);
    HideStockChrome();
    if (!face_ready_) return;
    // Catch activation status / any digit run in status text.
    if (status && status[0]) {
        char digits[16] = {};
        int n = 0;
        for (const char* p = status; *p && n < 15; ++p) {
            if (*p >= '0' && *p <= '9') {
                digits[n++] = *p;
            } else if (n >= 4) {
                break;
            } else {
                n = 0;
            }
        }
        digits[n] = '\0';
        if (n >= 4 && n <= 8) {
            std::strncpy(activation_code_, digits, sizeof(activation_code_) - 1);
            activation_code_[sizeof(activation_code_) - 1] = '\0';
            activation_bound_ = false;
            Settings s("valera", true);
            s.SetString("act_code", activation_code_);
            s.SetInt("act_bound", 0);
        }
    }
    ReloadActivationFromNvs();
    if (settings_open_) RefreshSettings();
    else PaintActivationBanner();
}

void ValeraLcdDisplay::ShowNotification(const char* notification, int duration_ms) {
    (void)notification;
    (void)duration_ms;
}

void ValeraLcdDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void ValeraLcdDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) emotion = "neutral";
    std::strncpy(emotion_name_, emotion, sizeof(emotion_name_) - 1);
    emotion_name_[sizeof(emotion_name_) - 1] = '\0';
}

void ValeraLcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (!face_ready_) return;

    const char* text_in = (content != nullptr) ? content : "";
    if (role != nullptr && std::strcmp(role, "system") == 0 && text_in[0] != '\0') {
        char digits[16] = {};
        int n = 0;
        for (const char* p = text_in; *p && n < 15; ++p) {
            if (*p >= '0' && *p <= '9') {
                digits[n++] = *p;
            } else if (n >= 4) {
                break;
            } else {
                n = 0;
            }
        }
        digits[n] = '\0';
        if (n >= 4 && n <= 8) {
            std::strncpy(activation_code_, digits, sizeof(activation_code_) - 1);
            activation_code_[sizeof(activation_code_) - 1] = '\0';
            activation_bound_ = false;
            Settings s("valera", true);
            s.SetString("act_code", activation_code_);
            s.SetInt("act_bound", 0);
            ESP_LOGI(TAG, "Activation code saved: %s", activation_code_);
            PaintActivationBanner();
            if (settings_open_) RefreshSettings();
        }
    }

    if (caption_label_ == nullptr) return;
    if (text_in[0] == '\0' && !assistant_active_) {
        // Do NOT wipe a pending activation code when alerts dismiss.
        ReloadActivationFromNvs();
        PaintActivationBanner();
        caption_text_[0] = '\0';
        has_caption_ = false;
        caption_dirty_ = false;
        if (caption_label_) lv_label_set_text(caption_label_, "");
        return;
    }
    std::strncpy(caption_text_, text_in, sizeof(caption_text_) - 1);
    caption_text_[sizeof(caption_text_) - 1] = '\0';

    lv_color_t color = kWhite;
    if (role != nullptr) {
        if (std::strcmp(role, "user") == 0)
            color = kCyan;
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
