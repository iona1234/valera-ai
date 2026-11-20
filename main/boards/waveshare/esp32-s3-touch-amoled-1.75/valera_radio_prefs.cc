#include "valera_radio_prefs.h"

#include "settings.h"

#include <cstdio>
#include <string>

namespace ValeraRadioPrefs {
namespace {

constexpr const char* kNs = "valera";
constexpr const char* kKey = "radio_hide";

uint32_t g_hide_mask = 0;
bool g_loaded = false;

void EnsureLoaded() {
    if (g_loaded) return;
    Settings s(kNs, false);
    g_hide_mask = (uint32_t)s.GetInt(kKey, 0);
    g_loaded = true;
}

void Persist() {
    Settings s(kNs, true);
    s.SetInt(kKey, (int32_t)g_hide_mask);
}

}  // namespace

void Load() { EnsureLoaded(); }

bool IsHidden(int index) {
    EnsureLoaded();
    if (index < 0 || index >= kValeraRadioStationCount || index >= 32) return true;
    return (g_hide_mask & (1u << index)) != 0;
}

void SetHidden(int index, bool hidden) {
    EnsureLoaded();
    if (index < 0 || index >= kValeraRadioStationCount || index >= 32) return;
    if (hidden) g_hide_mask |= (1u << index);
    else g_hide_mask &= ~(1u << index);
    Persist();
}

void ClearHidden() {
    EnsureLoaded();
    g_hide_mask = 0;
    Persist();
}

int VisibleCount() {
    EnsureLoaded();
    int n = 0;
    for (int i = 0; i < kValeraRadioStationCount; ++i) {
        if (!IsHidden(i)) ++n;
    }
    return n;
}

std::string ListVisible() {
    EnsureLoaded();
    std::string out;
    for (int i = 0; i < kValeraRadioStationCount; ++i) {
        if (IsHidden(i)) continue;
        if (!out.empty()) out += "\n";
        char buf[24];
        snprintf(buf, sizeof(buf), "[%d] ", i);
        out += buf;
        out += kValeraRadioStations[i].name;
    }
    if (out.empty()) out = "(пусто — все скрыты)";
    return out;
}

std::string ListHidden() {
    EnsureLoaded();
    std::string out;
    for (int i = 0; i < kValeraRadioStationCount; ++i) {
        if (!IsHidden(i)) continue;
        if (!out.empty()) out += "\n";
        char buf[24];
        snprintf(buf, sizeof(buf), "[%d] ", i);
        out += buf;
        out += kValeraRadioStations[i].name;
    }
    if (out.empty()) out = "(нет скрытых)";
    return out;
}

}  // namespace ValeraRadioPrefs
