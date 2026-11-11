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
const lv_color_t kCyan = lv_color_hex(0x00E5FF);
const lv_color_t kMagenta = lv_color_hex(0xFF2BD6);
const lv_color_t kDim = lv_color_hex(0x1A1A22);
const lv_color_t kWhite = lv_color_hex(0xF2F2F2);

// 7-segment bit masks for digits 0-9 (a b c d e f g).
constexpr uint8_t kSegBits[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

const char* kWeekdaysRu[] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
const char* kMonthsRu[] = {"янв", "фев", "мар", "апр", "май", "июн",
                           "июл", "авг", "сен", "окт", "ноя", "дек"};

lv_obj_t* MakeSeg(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* s = lv_obj_create(parent);
    lv_obj_set_size(s, w, h);
    lv_obj_set_pos(s, x, y);
    lv_obj_set_style_radius(s, 3, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_set_style_bg_color(s, kDim, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
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
    timer_ = lv_timer_create(TickThunk, 40, this);
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

    root_ = lv_obj_create(screen);
    lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(root_, kBg, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // Digits: four 7-segment blocks centered horizontally.
    const int digit_w = 70;
    const int digit_h = 120;
    const int gap = 14;
    const int colon_w = 16;
    const int total_w = digit_w * 4 + gap * 3 + colon_w;
    const int origin_x = (LV_HOR_RES - total_w) / 2;
    const int origin_y = 78;
    const int thick = 10;
    const int inset = 8;

    int xs[4];
    xs[0] = origin_x;
    xs[1] = origin_x + digit_w + gap;
    xs[2] = xs[1] + digit_w + gap + colon_w;
    xs[3] = xs[2] + digit_w + gap;

    for (int d = 0; d < 4; ++d) {
        int x = xs[d];
        int y = origin_y;
        // a b c d e f g
        seg_[d][0] = MakeSeg(root_, x + inset, y, digit_w - 2 * inset, thick);                  // a
        seg_[d][1] = MakeSeg(root_, x + digit_w - thick, y + inset, thick, digit_h / 2 - inset); // b
        seg_[d][2] =
            MakeSeg(root_, x + digit_w - thick, y + digit_h / 2, thick, digit_h / 2 - inset);    // c
        seg_[d][3] =
            MakeSeg(root_, x + inset, y + digit_h - thick, digit_w - 2 * inset, thick);          // d
        seg_[d][4] = MakeSeg(root_, x, y + digit_h / 2, thick, digit_h / 2 - inset);             // e
        seg_[d][5] = MakeSeg(root_, x, y + inset, thick, digit_h / 2 - inset);                   // f
        seg_[d][6] =
            MakeSeg(root_, x + inset, y + digit_h / 2 - thick / 2, digit_w - 2 * inset, thick);  // g
    }

    colon_[0] = MakeSeg(root_, xs[1] + digit_w + (gap + colon_w - 10) / 2, origin_y + 34, 10, 10);
    colon_[1] = MakeSeg(root_, xs[1] + digit_w + (gap + colon_w - 10) / 2, origin_y + 76, 10, 10);
    lv_obj_set_style_bg_color(colon_[0], kCyan, 0);
    lv_obj_set_style_bg_color(colon_[1], kCyan, 0);

    // Eyes under the clock.
    for (int i = 0; i < 2; ++i) {
        eye_[i] = lv_obj_create(root_);
        lv_obj_set_size(eye_[i], 64, 64);
        lv_obj_set_pos(eye_[i], LV_HOR_RES / 2 - 80 + i * 96, 220);
        lv_obj_set_style_radius(eye_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(eye_[i], lv_color_hex(0x101018), 0);
        lv_obj_set_style_border_color(eye_[i], kCyan, 0);
        lv_obj_set_style_border_width(eye_[i], 2, 0);
        lv_obj_set_style_pad_all(eye_[i], 0, 0);
        lv_obj_clear_flag(eye_[i], LV_OBJ_FLAG_SCROLLABLE);

        iris_[i] = lv_obj_create(eye_[i]);
        lv_obj_set_size(iris_[i], 28, 28);
        lv_obj_center(iris_[i]);
        lv_obj_set_style_radius(iris_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(iris_[i], kMagenta, 0);
        lv_obj_set_style_border_width(iris_[i], 0, 0);
        lv_obj_set_style_pad_all(iris_[i], 0, 0);
        lv_obj_clear_flag(iris_[i], LV_OBJ_FLAG_SCROLLABLE);

        pupil_[i] = lv_obj_create(iris_[i]);
        lv_obj_set_size(pupil_[i], 12, 12);
        lv_obj_center(pupil_[i]);
        lv_obj_set_style_radius(pupil_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(pupil_[i], kWhite, 0);
        lv_obj_set_style_border_width(pupil_[i], 0, 0);
        lv_obj_clear_flag(pupil_[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    auto theme = static_cast<LvglTheme*>(current_theme_);
    const lv_font_t* font = theme && theme->text_font() ? theme->text_font()->font() : nullptr;

    date_label_ = lv_label_create(root_);
    if (font) lv_obj_set_style_text_font(date_label_, font, 0);
    lv_obj_set_style_text_color(date_label_, kCyan, 0);
    lv_obj_align(date_label_, LV_ALIGN_TOP_MID, 0, 42);
    lv_label_set_text(date_label_, "");

    // Climate pills.
    out_label_ = lv_label_create(root_);
    if (font) lv_obj_set_style_text_font(out_label_, font, 0);
    lv_obj_set_style_text_color(out_label_, kCyan, 0);
    lv_obj_set_style_bg_color(out_label_, lv_color_hex(0x0A2030), 0);
    lv_obj_set_style_bg_opa(out_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(out_label_, 18, 0);
    lv_obj_set_style_pad_hor(out_label_, 14, 0);
    lv_obj_set_style_pad_ver(out_label_, 6, 0);
    lv_obj_align(out_label_, LV_ALIGN_BOTTOM_MID, -100, -78);
    lv_label_set_text(out_label_, "улица …");

    room_label_ = lv_label_create(root_);
    if (font) lv_obj_set_style_text_font(room_label_, font, 0);
    lv_obj_set_style_text_color(room_label_, kMagenta, 0);
    lv_obj_set_style_bg_color(room_label_, lv_color_hex(0x2A1028), 0);
    lv_obj_set_style_bg_opa(room_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(room_label_, 18, 0);
    lv_obj_set_style_pad_hor(room_label_, 14, 0);
    lv_obj_set_style_pad_ver(room_label_, 6, 0);
    lv_obj_align(room_label_, LV_ALIGN_BOTTOM_MID, 100, -78);
    lv_label_set_text(room_label_, "комната …");

    caption_ = lv_label_create(root_);
    if (font) lv_obj_set_style_text_font(caption_, font, 0);
    lv_obj_set_style_text_color(caption_, kWhite, 0);
    lv_obj_set_width(caption_, LV_HOR_RES - 48);
    lv_obj_set_style_text_align(caption_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(caption_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(caption_, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_label_set_text(caption_, "");
    lv_obj_add_flag(caption_, LV_OBJ_FLAG_HIDDEN);

    // Thinking / speaking bars.
    for (int i = 0; i < 9; ++i) {
        bars_[i] = lv_obj_create(root_);
        lv_obj_set_size(bars_[i], 10, 16);
        lv_obj_set_pos(bars_[i], LV_HOR_RES / 2 - 70 + i * 16, 300);
        lv_obj_set_style_radius(bars_[i], 4, 0);
        lv_obj_set_style_bg_color(bars_[i], kCyan, 0);
        lv_obj_set_style_border_width(bars_[i], 0, 0);
        lv_obj_set_style_pad_all(bars_[i], 0, 0);
        lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bars_[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    ApplyMode(FaceMode::kIdle);
}

void ValeraLcdDisplay::TickThunk(lv_timer_t* timer) {
    auto* self = static_cast<ValeraLcdDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->Tick();
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
    if (climate.clock_fix_ok) {
        now += climate.clock_fix_sec;
    }
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);
    bool colon_on = ((tick_ / 12) % 2) == 0;
    PaintDigits(&tm_info, colon_on);

    if ((tick_ % 25) == 0) {
        PaintInfo();
    }

    // Eye animation.
    int look_x = 0;
    int look_y = 0;
    int iris = 28;
    lv_color_t iris_color = kMagenta;
    lv_color_t ring = kCyan;
    switch (mode_) {
        case FaceMode::kIdle:
            look_x = (int)(12.0f * sinf(tick_ * 0.04f));
            look_y = (int)(6.0f * sinf(tick_ * 0.027f));
            if ((tick_ % 90) < 6) {  // blink
                lv_obj_set_height(eye_[0], 8);
                lv_obj_set_height(eye_[1], 8);
            } else {
                lv_obj_set_height(eye_[0], 64);
                lv_obj_set_height(eye_[1], 64);
            }
            break;
        case FaceMode::kListen:
            look_x = (int)(10.0f * sinf(tick_ * 0.12f));
            look_y = 4;
            iris_color = kCyan;
            ring = kMagenta;
            break;
        case FaceMode::kSpeak:
            look_y = (int)(8.0f * sinf(tick_ * 0.2f));
            iris = 32;
            break;
        case FaceMode::kThink:
            look_x = (tick_ / 20) % 2 == 0 ? -10 : 10;
            look_y = -6;
            iris_color = lv_color_hex(0xA0A0FF);
            break;
        case FaceMode::kWifi:
            look_y = (int)(4.0f * sinf(tick_ * 0.15f));
            ring = kMagenta;
            break;
        case FaceMode::kSleep:
            lv_obj_set_height(eye_[0], 6);
            lv_obj_set_height(eye_[1], 6);
            break;
    }

    for (int i = 0; i < 2; ++i) {
        lv_obj_set_style_border_color(eye_[i], ring, 0);
        lv_obj_set_size(iris_[i], iris, iris);
        lv_obj_set_style_bg_color(iris_[i], iris_color, 0);
        lv_obj_align(iris_[i], LV_ALIGN_CENTER, look_x, look_y);
    }

    if (mode_ == FaceMode::kSpeak || mode_ == FaceMode::kThink || mode_ == FaceMode::kListen) {
        for (int i = 0; i < 9; ++i) {
            lv_obj_clear_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
            int h = 12 + (int)(28.0f * fabsf(sinf(tick_ * 0.25f + i * 0.7f)));
            lv_obj_set_height(bars_[i], h);
            lv_obj_set_style_bg_color(bars_[i], (i % 2) ? kMagenta : kCyan, 0);
            lv_obj_set_y(bars_[i], 316 - h);
        }
    } else {
        for (int i = 0; i < 9; ++i) {
            lv_obj_add_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

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

void ValeraLcdDisplay::PaintDigits(const struct tm* tm, bool colon_on) {
    if (tm == nullptr) return;
    int digits[4] = {tm->tm_hour / 10, tm->tm_hour % 10, tm->tm_min / 10, tm->tm_min % 10};
    for (int d = 0; d < 4; ++d) {
        uint8_t bits = kSegBits[digits[d] % 10];
        for (int s = 0; s < 7; ++s) {
            bool on = (bits & (1 << s)) != 0;
            lv_obj_set_style_bg_color(seg_[d][s], on ? kCyan : kDim, 0);
        }
    }
    lv_obj_set_style_bg_opa(colon_[0], colon_on ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_bg_opa(colon_[1], colon_on ? LV_OPA_COVER : LV_OPA_30, 0);
}

void ValeraLcdDisplay::PaintInfo() {
    time_t now = time(nullptr);
    ValeraClimate c = ValeraSensors::Get();
    if (c.clock_fix_ok) now += c.clock_fix_sec;
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);
    char date_buf[48];
    int wday = tm_info.tm_wday;
    if (wday < 0 || wday > 6) wday = 0;
    int mon = tm_info.tm_mon;
    if (mon < 0 || mon > 11) mon = 0;
    std::snprintf(date_buf, sizeof(date_buf), "%s, %d %s", kWeekdaysRu[wday], tm_info.tm_mday,
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
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_height(eye_[i], 64);
    }
}

void ValeraLcdDisplay::SetStatus(const char* status) {
    if (status == nullptr) return;
    // Keep stock status updates available but prefer face caption for chat.
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
    // Face owns the chrome; skip stock status-bar clock overwrite.
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
