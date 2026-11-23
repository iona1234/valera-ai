#include "valera_face.h"

#include "application.h"
#include "audio/audio_codec.h"
#include "board.h"
#include "boards/common/backlight.h"
#include "device_state.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "settings.h"
#include "valera_radio_prefs.h"
#include "valera_radio_stations.h"
#include "valera_sensors.h"

#include <esp_log.h>
#include <wifi_manager.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#define TAG "ValeraFace"

namespace {

const lv_color_t kBg = lv_color_hex(0x000000);
const lv_color_t kCard = lv_color_hex(0x1A1F27);
const lv_color_t kWhite = lv_color_hex(0xFFFFFF);
const lv_color_t kCyan = lv_color_hex(0x7FE7FF);
const lv_color_t kMuted = lv_color_hex(0xA8B0BA);
const lv_color_t kPink = lv_color_hex(0xFF8FA3);

const char* kDow[] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
const char* kMon[] = {"янв", "фев", "мар", "апр", "мая", "июн",
                      "июл", "авг", "сен", "окт", "ноя", "дек"};

int Clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

}  // namespace

ValeraLcdDisplay::ValeraLcdDisplay(esp_lcd_panel_io_handle_t io_handle,
                                   esp_lcd_panel_handle_t panel_handle, int width, int height,
                                   int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                   bool swap_xy)
    : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

void ValeraLcdDisplay::RounderEventCb(lv_event_t* e) {
    auto* area = static_cast<lv_area_t*>(lv_event_get_param(e));
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

const lv_font_t* ValeraLcdDisplay::Font() const {
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    if (theme && theme->text_font()) return theme->text_font()->font();
    return nullptr;
}

lv_obj_t* ValeraLcdDisplay::MakePage() {
    lv_obj_t* page = lv_obj_create(root_);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_color(page, kBg, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    return page;
}

lv_obj_t* ValeraLcdDisplay::MakeLabel(lv_obj_t* parent, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, (int)(LV_HOR_RES * 0.72f));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, color, 0);
    if (const lv_font_t* font = Font()) lv_obj_set_style_text_font(label, font, 0);
    return label;
}

lv_obj_t* ValeraLcdDisplay::MakeButton(lv_obj_t* parent, int w, int h, const char* text, int id) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, h / 2, 0);
    lv_obj_set_style_bg_color(btn, kCard, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<intptr_t>(id)));
    lv_obj_add_event_cb(btn, ButtonCb, LV_EVENT_CLICKED, this);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, kWhite, 0);
    if (const lv_font_t* font = Font()) lv_obj_set_style_text_font(label, font, 0);
    lv_obj_center(label);
    return btn;
}

void ValeraLcdDisplay::HideStockChrome() {
    auto hide = [](lv_obj_t* obj) {
        if (obj) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
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
    hide(container_);
}

void ValeraLcdDisplay::Build() {
    auto screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, kBg, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    root_ = lv_obj_create(screen);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(root_, kBg, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(root_);

    for (int i = 0; i < (int)Page::kCount; ++i) page_[i] = MakePage();

    const int W = LV_HOR_RES;

    // Clock
    {
        lv_obj_t* page = page_[(int)Page::kClock];
        clock_date_ = MakeLabel(page, kMuted);
        lv_obj_align(clock_date_, LV_ALIGN_CENTER, 0, -78);
        clock_time_ = MakeLabel(page, kWhite);
        lv_obj_set_width(clock_time_, (int)(W * 0.9f));
        lv_label_set_long_mode(clock_time_, LV_LABEL_LONG_CLIP);
        lv_obj_align(clock_time_, LV_ALIGN_CENTER, 0, -8);
        lv_obj_set_style_transform_pivot_x(clock_time_, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(clock_time_, lv_pct(50), 0);
        lv_obj_set_style_transform_scale(clock_time_, 512, 0);
        clock_foot_ = MakeLabel(page, kCyan);
        lv_obj_align(clock_foot_, LV_ALIGN_CENTER, 0, 88);
        lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(page, GestureCb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(page, GestureCb, LV_EVENT_RELEASED, this);
    }

    // Weather
    {
        lv_obj_t* page = page_[(int)Page::kWeather];
        auto* title = MakeLabel(page, kMuted);
        lv_label_set_text(title, "Погода");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -90);
        weather_temp_ = MakeLabel(page, kWhite);
        lv_obj_align(weather_temp_, LV_ALIGN_CENTER, 0, -20);
        weather_cond_ = MakeLabel(page, kCyan);
        lv_obj_align(weather_cond_, LV_ALIGN_CENTER, 0, 36);
        weather_room_ = MakeLabel(page, kMuted);
        lv_obj_align(weather_room_, LV_ALIGN_CENTER, 0, 88);
        lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(page, GestureCb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(page, GestureCb, LV_EVENT_RELEASED, this);
    }

    // Radio — selection only. Live decode is not wired.
    {
        lv_obj_t* page = page_[(int)Page::kRadio];
        auto* title = MakeLabel(page, kMuted);
        lv_label_set_text(title, "Радио");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -100);
        radio_name_ = MakeLabel(page, kWhite);
        lv_obj_align(radio_name_, LV_ALIGN_CENTER, 0, -28);
        radio_meta_ = MakeLabel(page, kMuted);
        lv_obj_align(radio_meta_, LV_ALIGN_CENTER, 0, 36);
        lv_obj_t* btn = MakeButton(page, 180, 56, "Станции", 50);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, 120);
        lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(page, GestureCb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(page, GestureCb, LV_EVENT_RELEASED, this);
    }

    // Stations — one card, three centered controls.
    {
        lv_obj_t* page = page_[(int)Page::kStations];
        station_index_ = MakeLabel(page, kMuted);
        lv_obj_align(station_index_, LV_ALIGN_CENTER, 0, -110);
        station_name_ = MakeLabel(page, kWhite);
        lv_obj_align(station_name_, LV_ALIGN_CENTER, 0, -36);
        station_meta_ = MakeLabel(page, kCyan);
        lv_obj_align(station_meta_, LV_ALIGN_CENTER, 0, 24);

        const int side = 64;
        const int mid = 148;
        const int gap = 12;
        const int row_w = side + gap + mid + gap + side;
        lv_obj_t* row = lv_obj_create(page);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, row_w, side);
        lv_obj_align(row, LV_ALIGN_CENTER, 0, 120);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        MakeButton(row, side, side, "<", 40);
        lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 0, 0);
        MakeButton(row, mid, side, "Выбрать", 42);
        lv_obj_align(lv_obj_get_child(row, 1), LV_ALIGN_CENTER, 0, 0);
        MakeButton(row, side, side, ">", 41);
        lv_obj_align(lv_obj_get_child(row, 2), LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(page, GestureCb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(page, GestureCb, LV_EVENT_RELEASED, this);
    }

    // Settings — full screen, never shares the frame with stations.
    {
        lv_obj_t* page = page_[(int)Page::kSettings];
        auto* title = MakeLabel(page, kWhite);
        lv_label_set_text(title, "Настройки");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -130);

        auto row = [&](int y, const char* name, int minus_id, int plus_id, lv_obj_t** value) {
            auto* cap = MakeLabel(page, kMuted);
            lv_label_set_text(cap, name);
            lv_obj_align(cap, LV_ALIGN_CENTER, 0, y - 28);
            const int side = 56;
            const int mid = 88;
            const int gap = 16;
            const int row_w = side + gap + mid + gap + side;
            lv_obj_t* box = lv_obj_create(page);
            lv_obj_remove_style_all(box);
            lv_obj_set_size(box, row_w, side);
            lv_obj_align(box, LV_ALIGN_CENTER, 0, y + 16);
            MakeButton(box, side, side, "-", minus_id);
            lv_obj_align(lv_obj_get_child(box, 0), LV_ALIGN_LEFT_MID, 0, 0);
            *value = MakeLabel(box, kWhite);
            lv_obj_set_width(*value, mid);
            lv_obj_align(*value, LV_ALIGN_CENTER, 0, 0);
            MakeButton(box, side, side, "+", plus_id);
            lv_obj_align(lv_obj_get_child(box, 2), LV_ALIGN_RIGHT_MID, 0, 0);
        };
        row(-80, "Яркость", 10, 11, &set_bri_);
        row(20, "Громкость", 20, 21, &set_vol_);

        lv_obj_t* code = MakeButton(page, 180, 48, "Код", 30);
        lv_obj_align(code, LV_ALIGN_CENTER, 0, 115);
        lv_obj_t* back = MakeButton(page, 140, 44, "Назад", 31);
        lv_obj_align(back, LV_ALIGN_CENTER, 0, 175);
    }

    // Activation code — opaque full screen, created last so it stays on top.
    {
        lv_obj_t* page = page_[(int)Page::kCode];
        auto* title = MakeLabel(page, kMuted);
        lv_label_set_text(title, "КОД АКТИВАЦИИ");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -90);
        code_digits_ = MakeLabel(page, kCyan);
        lv_obj_set_width(code_digits_, (int)(W * 0.84f));
        lv_obj_align(code_digits_, LV_ALIGN_CENTER, 0, -10);
        lv_obj_set_style_transform_pivot_x(code_digits_, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(code_digits_, lv_pct(50), 0);
        lv_obj_set_style_transform_scale(code_digits_, 512, 0);
        code_hint_ = MakeLabel(page, kPink);
        lv_obj_align(code_hint_, LV_ALIGN_CENTER, 0, 78);
        lv_obj_t* ok = MakeButton(page, 160, 52, "OK", 70);
        lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, -36);
    }

    // Assistant status — one centered word, no eyes.
    {
        lv_obj_t* page = page_[(int)Page::kStatus];
        status_label_ = MakeLabel(page, kWhite);
        lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(status_label_, "...");
    }
}

void ValeraLcdDisplay::ShowPage(Page page) {
    page_ = page;
    for (int i = 0; i < (int)Page::kCount; ++i) {
        if (!page_[i]) continue;
        if (i == (int)page) lv_obj_clear_flag(page_[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page_[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (page_[ (int)page ]) lv_obj_move_foreground(page_[(int)page]);
    if (page == Page::kClock) PaintClock();
    else if (page == Page::kWeather) PaintWeather();
    else if (page == Page::kRadio) PaintRadio();
    else if (page == Page::kStations) PaintStations();
    else if (page == Page::kSettings) PaintSettings();
    else if (page == Page::kCode) PaintCode();
}

void ValeraLcdDisplay::StepCarousel(int dir) {
    static const Page kOrder[] = {Page::kClock, Page::kWeather, Page::kRadio, Page::kStations};
    int idx = 0;
    for (int i = 0; i < 4; ++i) {
        if (kOrder[i] == page_) idx = i;
    }
    idx = (idx + dir) % 4;
    if (idx < 0) idx += 4;
    return_page_ = kOrder[idx];
    ShowPage(kOrder[idx]);
}

void ValeraLcdDisplay::PaintClock() {
    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);
    char buf[32];
    const int hour = tm_info.tm_hour;
    const int min = tm_info.tm_min;
    const bool valid = (tm_info.tm_year + 1900) >= 2024;
    if (clock_time_) {
        if (valid) snprintf(buf, sizeof(buf), "%02d:%02d", hour, min);
        else snprintf(buf, sizeof(buf), "--:--");
        lv_label_set_text(clock_time_, buf);
        lv_obj_set_style_text_color(clock_time_, lv_color_hex(clock_color_), 0);
    }
    if (clock_date_) {
        if (valid && tm_info.tm_wday >= 0 && tm_info.tm_wday < 7 && tm_info.tm_mon >= 0 &&
            tm_info.tm_mon < 12) {
            snprintf(buf, sizeof(buf), "%s  %d %s", kDow[tm_info.tm_wday], tm_info.tm_mday,
                     kMon[tm_info.tm_mon]);
        } else {
            snprintf(buf, sizeof(buf), "часы");
        }
        lv_label_set_text(clock_date_, buf);
    }
    if (clock_foot_) {
        if (!activation_bound_ && activation_code_[0] != '\0') {
            snprintf(buf, sizeof(buf), "КОД  %s", activation_code_);
            lv_label_set_text(clock_foot_, buf);
        } else if (!WifiManager::GetInstance().IsConnected()) {
            lv_label_set_text(clock_foot_, "Wi-Fi MyWiFi");
        } else {
            lv_label_set_text(clock_foot_, "свайп");
        }
    }
}

void ValeraLcdDisplay::PaintWeather() {
    ValeraClimate c = ValeraSensors::Get();
    char buf[48];
    if (weather_temp_) {
        if (c.outdoor_ok) snprintf(buf, sizeof(buf), "%.0f°", c.outdoor_c);
        else snprintf(buf, sizeof(buf), "--°");
        lv_label_set_text(weather_temp_, buf);
    }
    if (weather_cond_) lv_label_set_text(weather_cond_, c.condition[0] ? c.condition : "погода");
    if (weather_room_) {
        if (c.room_ok) snprintf(buf, sizeof(buf), "комната  %.0f°", c.room_c);
        else snprintf(buf, sizeof(buf), "Барнаул");
        lv_label_set_text(weather_room_, buf);
    }
}

void ValeraLcdDisplay::PaintRadio() {
    if (station_idx_ < 0 || station_idx_ >= kValeraRadioStationCount) station_idx_ = 0;
    const auto& st = kValeraRadioStations[station_idx_];
    if (radio_name_) lv_label_set_text(radio_name_, st.name);
    if (radio_meta_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s\nзвук эфира ещё не подключён", st.genre);
        lv_label_set_text(radio_meta_, buf);
    }
}

void ValeraLcdDisplay::PaintStations() {
    if (kValeraRadioStationCount <= 0) return;
    ValeraRadioPrefs::Load();
    if (station_idx_ < 0 || station_idx_ >= kValeraRadioStationCount ||
        ValeraRadioPrefs::IsHidden(station_idx_)) {
        StepStation(0);
    }
    const auto& st = kValeraRadioStations[station_idx_];
    int visible = 0;
    int place = 0;
    for (int i = 0; i < kValeraRadioStationCount; ++i) {
        if (ValeraRadioPrefs::IsHidden(i)) continue;
        ++visible;
        if (i <= station_idx_) ++place;
    }
    char buf[48];
    if (station_index_) {
        snprintf(buf, sizeof(buf), "%d / %d", place, visible > 0 ? visible : 1);
        lv_label_set_text(station_index_, buf);
    }
    if (station_name_) lv_label_set_text(station_name_, st.name);
    if (station_meta_) {
        snprintf(buf, sizeof(buf), "%s   %d kbps", st.genre, st.kbps);
        lv_label_set_text(station_meta_, buf);
    }
}

void ValeraLcdDisplay::PaintSettings() {
    char buf[16];
    if (set_bri_) {
        snprintf(buf, sizeof(buf), "%d", brightness_);
        lv_label_set_text(set_bri_, buf);
    }
    if (set_vol_) {
        snprintf(buf, sizeof(buf), "%d", volume_);
        lv_label_set_text(set_vol_, buf);
    }
}

void ValeraLcdDisplay::PaintCode() {
    if (!code_digits_) return;
    if (!activation_bound_ && activation_code_[0] != '\0') {
        lv_label_set_text(code_digits_, activation_code_);
        if (code_hint_) lv_label_set_text(code_hint_, "xiaozhi.me");
    } else if (activation_bound_) {
        lv_label_set_text(code_digits_, "OK");
        if (code_hint_) lv_label_set_text(code_hint_, "уже привязано");
    } else {
        lv_label_set_text(code_digits_, "......");
        if (code_hint_) lv_label_set_text(code_hint_, "ждём сервер");
    }
}

void ValeraLcdDisplay::PaintStatus(const char* text) {
    if (status_label_) lv_label_set_text(status_label_, text ? text : "");
}

void ValeraLcdDisplay::ReloadActivation() {
    Settings s("valera", false);
    activation_bound_ = s.GetInt("act_bound", 0) != 0;
    std::string saved = s.GetString("act_code");
    if (!activation_bound_ && saved.size() >= 4 && saved.size() < sizeof(activation_code_)) {
        std::strncpy(activation_code_, saved.c_str(), sizeof(activation_code_) - 1);
        activation_code_[sizeof(activation_code_) - 1] = '\0';
    } else if (activation_bound_) {
        activation_code_[0] = '\0';
    }
}

void ValeraLcdDisplay::CaptureCode(const char* text) {
    if (text == nullptr || text[0] == '\0') return;
    char digits[16] = {};
    int n = 0;
    for (const char* p = text; *p && n < 15; ++p) {
        if (*p >= '0' && *p <= '9') {
            digits[n++] = *p;
        } else if (n >= 4) {
            break;
        } else {
            n = 0;
        }
    }
    digits[n] = '\0';
    if (n < 4 || n > 8) return;
    std::strncpy(activation_code_, digits, sizeof(activation_code_) - 1);
    activation_code_[sizeof(activation_code_) - 1] = '\0';
    activation_bound_ = false;
    Settings s("valera", true);
    s.SetString("act_code", activation_code_);
    s.SetInt("act_bound", 0);
    ESP_LOGI(TAG, "Activation code %s", activation_code_);
    ShowPage(Page::kCode);
}

void ValeraLcdDisplay::NudgeBrightness(int delta) {
    auto* light = Board::GetInstance().GetBacklight();
    if (light == nullptr) return;
    brightness_ = Clamp((int)light->brightness() + delta, 10, 100);
    light->SetBrightness((uint8_t)brightness_, true);
    PaintSettings();
}

void ValeraLcdDisplay::NudgeVolume(int delta) {
    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) return;
    volume_ = Clamp(codec->output_volume() + delta, 0, 100);
    codec->SetOutputVolume(volume_);
    PaintSettings();
}

void ValeraLcdDisplay::StepStation(int delta) {
    if (kValeraRadioStationCount <= 0) return;
    ValeraRadioPrefs::Load();
    if (station_idx_ < 0 || station_idx_ >= kValeraRadioStationCount) station_idx_ = 0;
    int idx = station_idx_;
    if (delta == 0) {
        if (!ValeraRadioPrefs::IsHidden(idx)) return;
        delta = 1;
    }
    for (int n = 0; n < kValeraRadioStationCount; ++n) {
        idx += delta;
        while (idx < 0) idx += kValeraRadioStationCount;
        while (idx >= kValeraRadioStationCount) idx -= kValeraRadioStationCount;
        if (!ValeraRadioPrefs::IsHidden(idx)) {
            station_idx_ = idx;
            break;
        }
    }
}

void ValeraLcdDisplay::LoadClockStyle() {
    Settings s("valera", false);
    clock_color_ = (uint32_t)s.GetInt("clock_hh", 0xFFFFFF);
    station_idx_ = s.GetInt("radio_idx", 0);
    if (auto* light = Board::GetInstance().GetBacklight()) brightness_ = light->brightness();
    if (auto* codec = Board::GetInstance().GetAudioCodec()) volume_ = codec->output_volume();
    ReloadActivation();
}

void ValeraLcdDisplay::SetClockStyle(int look, int scale_pct, uint32_t hh_color, uint32_t mm_color) {
    (void)scale_pct;
    (void)mm_color;
    static const uint32_t kLooks[] = {0xFFFFFF, 0x7FE7FF, 0xFFE8EE, 0xF5C400, 0x7CFFB2};
    clock_color_ = (look >= 0 && look < 5) ? kLooks[look] : hh_color;
    Settings s("valera", true);
    s.SetInt("clock_hh", (int32_t)clock_color_);
    if (face_ready_ && page_ == Page::kClock) PaintClock();
}

void ValeraLcdDisplay::OpenQuickShade() { ShowPage(Page::kSettings); }

void ValeraLcdDisplay::GestureCb(lv_event_t* e) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_event_get_user_data(e));
    if (self) self->OnGesture(e);
}

void ValeraLcdDisplay::OnGesture(lv_event_t* e) {
    if (page_ == Page::kSettings || page_ == Page::kCode || page_ == Page::kStatus) return;
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) return;
    lv_point_t p{};
    lv_indev_get_point(indev, &p);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        gesture_down_ = true;
        gesture_x_ = (int16_t)p.x;
        gesture_y_ = (int16_t)p.y;
        return;
    }
    if (code != LV_EVENT_RELEASED || !gesture_down_) return;
    gesture_down_ = false;
    const int dx = p.x - gesture_x_;
    const int dy = p.y - gesture_y_;
    const int adx = dx < 0 ? -dx : dx;
    const int ady = dy < 0 ? -dy : dy;
    if (gesture_y_ < 140 && dy > 50 && ady > adx) {
        return_page_ = page_;
        ShowPage(Page::kSettings);
        return;
    }
    if (adx > 60 && adx > ady) StepCarousel(dx < 0 ? 1 : -1);
}

void ValeraLcdDisplay::ButtonCb(lv_event_t* e) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_event_get_user_data(e));
    if (self == nullptr || lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    self->OnButton((int)(intptr_t)lv_obj_get_user_data(target));
}

void ValeraLcdDisplay::OnButton(int id) {
    if (id == 10) NudgeBrightness(-10);
    else if (id == 11) NudgeBrightness(10);
    else if (id == 20) NudgeVolume(-10);
    else if (id == 21) NudgeVolume(10);
    else if (id == 30) {
        ReloadActivation();
        ShowPage(Page::kCode);
    } else if (id == 31) {
        Page back = return_page_;
        if (back == Page::kSettings || back == Page::kCode || back == Page::kStatus)
            back = Page::kClock;
        ShowPage(back);
    } else if (id == 40) {
        StepStation(-1);
        PaintStations();
    } else if (id == 41) {
        StepStation(1);
        PaintStations();
    } else if (id == 42) {
        Settings s("valera", true);
        s.SetInt("radio_idx", station_idx_);
        ShowPage(Page::kRadio);
    } else if (id == 50) {
        ShowPage(Page::kStations);
    } else if (id == 70) {
        Page back = return_page_;
        if (back == Page::kCode || back == Page::kSettings || back == Page::kStatus)
            back = Page::kClock;
        ShowPage(back);
    }
}

void ValeraLcdDisplay::TickThunk(lv_timer_t* timer) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->Tick();
}

void ValeraLcdDisplay::Tick() {
    if (!face_ready_) return;
    ++tick_;
    const auto state = Application::GetInstance().GetDeviceState();
    const bool busy = state == kDeviceStateListening || state == kDeviceStateSpeaking ||
                      state == kDeviceStateConnecting;
    if (busy && page_ != Page::kCode) {
        if (page_ != Page::kStatus) {
            return_page_ = page_;
            ShowPage(Page::kStatus);
        }
        const char* text = "Связь";
        if (state == kDeviceStateListening) text = "Слушаю";
        else if (state == kDeviceStateSpeaking) text = "Говорю";
        PaintStatus(text);
        return;
    }
    if (page_ == Page::kStatus) {
        Page back = return_page_;
        if (back == Page::kStatus) back = Page::kClock;
        ShowPage(back);
    }
    if (page_ == Page::kClock) PaintClock();
    else if (page_ == Page::kWeather && (tick_ % 20) == 0) PaintWeather();
    else if (page_ == Page::kCode && (tick_ % 3) == 0) {
        ReloadActivation();
        PaintCode();
        if (activation_bound_) ShowPage(Page::kClock);
    }
}

void ValeraLcdDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();
    DisplayLockGuard lock(this);
    lv_display_add_event_cb(display_, RounderEventCb, LV_EVENT_INVALIDATE_AREA, nullptr);
    HideStockChrome();
    LoadClockStyle();
    Build();
    face_ready_ = true;
    if (!activation_bound_ && activation_code_[0] != '\0') ShowPage(Page::kCode);
    else ShowPage(Page::kClock);
    timer_ = lv_timer_create(TickThunk, 1000, this);
}

void ValeraLcdDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    HideStockChrome();
    if (!face_ready_) return;
    CaptureCode(status);
}

void ValeraLcdDisplay::ShowNotification(const char* notification, int duration_ms) {
    (void)duration_ms;
    DisplayLockGuard lock(this);
    if (face_ready_) CaptureCode(notification);
}

void ValeraLcdDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void ValeraLcdDisplay::SetEmotion(const char* emotion) { (void)emotion; }

void ValeraLcdDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    if (!face_ready_) return;
    if (role != nullptr && std::strcmp(role, "system") == 0) CaptureCode(content);
}

void ValeraLcdDisplay::ClearChatMessages() {}

void ValeraLcdDisplay::SetHideSubtitle(bool hide) { (void)hide; }

void ValeraLcdDisplay::UpdateStatusBar(bool update_all) { (void)update_all; }

void ValeraLcdDisplay::SetPowerSaveMode(bool on) {
    // Never blank or rebuild the face. Sleep looked like a reboot.
    (void)on;
}

void ValeraLcdDisplay::SetTheme(Theme* theme) { SpiLcdDisplay::SetTheme(theme); }
