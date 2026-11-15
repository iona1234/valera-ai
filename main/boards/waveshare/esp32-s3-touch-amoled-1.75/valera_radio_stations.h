#pragma once

// Preset Russian internet-radio stations for Valera (AMOLED 1.75).
// Streams were HTTP-probed from this environment (200 + audio/*) on 2026-11-15.
// Playback pipeline is not wired yet — this list feeds the UI / future radio task.

struct ValeraRadioStation {
    const char* name;
    const char* genre;
    const char* url;
    const char* codec;  // "mp3" or "aac"
    int kbps;
    uint32_t color;
};

// Popka-style Browse folders (RU only).
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

// Favourites / default dial order (swipe L/R on radio poster).
static constexpr ValeraRadioStation kValeraRadioStations[] = {
    {"Наше Радио", "rock", "https://nashe1.hostingradio.ru/nashe-128.mp3", "mp3", 128, 0xE2483D},
    {"Европа Плюс", "hits", "https://ep128.hostingradio.ru:8030/ep128", "mp3", 128, 0x3E7BDB},
    {"Европа Плюс Москва", "hits", "https://emgregion.hostingradio.ru:8064/moscow.europaplus.mp3",
     "mp3", 128, 0x5FA8A0},
    {"Maximum", "rock", "https://maximum.hostingradio.ru/maximum96.aacp", "aac", 96, 0xF2994A},
    {"DFM", "dance", "https://dfm.hostingradio.ru/dfm96.aacp", "aac", 96, 0x9B5CFF},
    {"Hit FM", "hits", "https://hitfm.hostingradio.ru/hitfm96.aacp", "aac", 96, 0xFF6A13},
    {"Radio Record", "dance", "https://radiorecord.hostingradio.ru/rr_main96.aacp", "aac", 96,
     0xE6007E},
    {"Record VIP House", "dance", "https://radiorecord.hostingradio.ru/vip96.aacp", "aac", 96,
     0xC9A0FF},
    {"Record Techno", "dance", "https://radiorecord.hostingradio.ru/techno96.aacp", "aac", 96,
     0x0E8C8A},
    {"Вести FM", "news", "http://icecast.vgtrk.cdnvideo.ru/vestifm_mp3_192kbps", "mp3", 192,
     0x2E6FE0},
    {"Маяк", "news", "http://icecast.vgtrk.cdnvideo.ru/mayakfm_mp3_192kbps", "mp3", 192, 0xF5C400},
};

static constexpr int kValeraRadioStationCount =
    (int)(sizeof(kValeraRadioStations) / sizeof(kValeraRadioStations[0]));
