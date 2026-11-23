#pragma once

#include "valera_radio_stations.h"

#include <cstdint>
#include <string>

// NVS-backed hide mask for Valera RU radio presets (bit i = station i hidden).
namespace ValeraRadioPrefs {

void Load();
bool IsHidden(int index);
void SetHidden(int index, bool hidden);
void ClearHidden();
int VisibleCount();
std::string ListVisible();  // human-readable for MCP
std::string ListHidden();

}  // namespace ValeraRadioPrefs
