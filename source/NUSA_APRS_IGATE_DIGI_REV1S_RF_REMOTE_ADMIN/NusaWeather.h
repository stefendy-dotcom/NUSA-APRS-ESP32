#pragma once
#include <Arduino.h>
#include "AX25.h"

struct NusaWeatherData {
  bool valid=false;
  char source[12]{};
  uint32_t whenMs=0;
  bool tempValid=false; float tempC=0;
  bool humidityValid=false; int humidity=0;
  bool pressureValid=false; float pressureHpa=0;
  bool windDirValid=false; int windDirDeg=0;
  bool windSpeedValid=false; float windSpeedMph=0;
  bool gustValid=false; float gustMph=0;
  bool rain1hValid=false; float rain1hMm=0;
  bool rain24hValid=false; float rain24hMm=0;
  bool rainMidnightValid=false; float rainMidnightMm=0;
};

bool NusaWeather_parse(const AX25Msg &m, NusaWeatherData &out);
