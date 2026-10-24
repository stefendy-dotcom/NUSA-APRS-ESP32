#pragma once
#include <Arduino.h>
#include "AX25.h"

// Extract a station position from common APRS position packet formats.
// Supported: uncompressed and compressed position reports using !, =, /, @.
// Returns true only when latitude/longitude are valid.
bool NusaAprsPosition_parse(const AX25Msg &msg, double &lat, double &lon);
