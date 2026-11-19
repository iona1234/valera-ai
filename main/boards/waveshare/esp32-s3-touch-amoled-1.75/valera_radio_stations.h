#pragma once

#include <cstdint>

// Russian-only internet-radio presets for Valera (AMOLED 1.75).
// Each URL returned HTTP 200 + audio/* when probed on 2026-11-15.
// No foreign/Popka Paradise stations.
// Poster UI (Popka-style): ICY StreamTitle → song marquee; station list shows
// now-playing under the live station. Decode/ICY net path still to wire.

struct ValeraRadioStation {
    const char* name;
    const char* genre;
    const char* url;
    const char* codec;  // "mp3" or "aac"
    int kbps;
    uint32_t color;
};

enum ValeraRadioFolder : uint8_t {
    kValeraRadioFav = 0,
    kValeraRadioHits,
    kValeraRadioDance,
    kValeraRadioNews,
    kValeraRadioFolderCount,
};

static constexpr const char* kValeraRadioFolderName[kValeraRadioFolderCount] = {
    "Избранное",
    "Хиты",
    "Танцы",
    "Новости",
};

static constexpr uint32_t kValeraRadioFolderColor[kValeraRadioFolderCount] = {
    0xFF8BCB,
    0xFF8A3D,
    0x7CC47F,
    0x8FB6FF,
};

static constexpr ValeraRadioStation kValeraRadioStations[] = {
    {"Наше Радио", "rock", "https://nashe1.hostingradio.ru/nashe-128.mp3", "mp3", 128, 0xE2483D},
    {"Европа Плюс", "hits", "https://ep128.hostingradio.ru:8030/ep128", "mp3", 128, 0x3E7BDB},
    {"Европа Плюс Москва", "hits",
     "https://emgregion.hostingradio.ru:8064/moscow.europaplus.mp3", "mp3", 128, 0x5FA8A0},
    {"Русское Радио", "hits", "https://rusradio.hostingradio.ru/rusradio128.mp3", "mp3", 128,
     0xC0392B},
    {"Maximum", "rock", "https://maximum.hostingradio.ru/maximum96.aacp", "aac", 96, 0xF2994A},
    {"DFM", "dance", "https://dfm.hostingradio.ru/dfm96.aacp", "aac", 96, 0x9B5CFF},
    {"Hit FM", "hits", "https://hitfm.hostingradio.ru/hitfm96.aacp", "aac", 96, 0xFF6A13},
    {"Radio Record", "dance", "https://radiorecord.hostingradio.ru/rr_main96.aacp", "aac", 96,
     0xE6007E},
    {"Record VIP House", "dance", "https://radiorecord.hostingradio.ru/vip96.aacp", "aac", 96,
     0xC9A0FF},
    {"Record Techno", "dance", "https://radiorecord.hostingradio.ru/techno96.aacp", "aac", 96,
     0x0E8C8A},
    {"Record Mix", "dance", "https://radiorecord.hostingradio.ru/mix96.aacp", "aac", 96, 0x7CC47F},
    {"Record Russian Hits", "hits", "https://radiorecord.hostingradio.ru/russianhits96.aacp",
     "aac", 96, 0xFF8A65},
    {"Record Trap", "dance", "https://radiorecord.hostingradio.ru/trap96.aacp", "aac", 96,
     0x8FB6FF},
    {"Record House Hits", "dance", "https://radiorecord.hostingradio.ru/househits96.aacp", "aac",
     96, 0xF5C400},
    {"Record Synthwave", "dance", "https://radiorecord.hostingradio.ru/synth96.aacp", "aac", 96,
     0x5B8DEF},
    {"Record Club", "dance", "https://radiorecord.hostingradio.ru/club96.aacp", "aac", 96,
     0xFF6B6B},
    {"Монте-Карло", "lounge", "https://montecarlo.hostingradio.ru/montecarlo96.aacp", "aac", 96,
     0xD4AF37},
    {"Ретро FM", "hits", "https://retro.hostingradio.ru:8043/retro256.mp3", "mp3", 256, 0xC45C26},
    {"Дорожное радио", "hits", "https://dorognoe.hostingradio.ru/radio", "mp3", 128, 0x2E86AB},
    {"Шансон", "chanson", "https://chanson.hostingradio.ru:8041/chanson128.mp3", "mp3", 128,
     0x8B4513},
    {"Серебряный Дождь", "talk", "https://silverrain.hostingradio.ru/silver128.mp3", "mp3", 128,
     0xA0AEC0},
    {"Вести FM", "news", "http://icecast.vgtrk.cdnvideo.ru/vestifm_mp3_192kbps", "mp3", 192,
     0x2E6FE0},
    {"Маяк", "news", "http://icecast.vgtrk.cdnvideo.ru/mayakfm_mp3_192kbps", "mp3", 192,
     0xF5C400},
};

static constexpr int kValeraRadioStationCount =
    (int)(sizeof(kValeraRadioStations) / sizeof(kValeraRadioStations[0]));
