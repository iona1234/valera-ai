#pragma once

#include <string>

// Room probe is optional. Outdoor weather is Open-Meteo for Barnaul.
// Clock fix compares that local timestamp with time() so a console
// timezone already baked into settimeofday is not applied twice.
struct ValeraClimate {
    bool room_ok = false;
    float room_c = 0;
    float room_rh = 0;
    bool outdoor_ok = false;
    float outdoor_c = 0;
    int outdoor_rh = -1;
    int weather_code = -1;
    int clock_fix_sec = 0;
    bool clock_fix_ok = false;
    char condition[24] = "погода";
};

class ValeraSensors {
public:
    static void Start();
    static ValeraClimate Get();
    static std::string Describe();
};
