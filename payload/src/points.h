// points.h - the "Shot points" slider of the Troll page (stage D10): how many points each basket of yours is set to.
//
// The slider has 12 stops: 1, 2, 3 ... 11, and then 999. The menu stores the STOP NUMBER (1 .. 12) as a plain slider value
// (so the pointer code, the picture and the saved settings all treat it like every other slider); this file turns a stop into the number of points.
// Nothing here touches the game.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace tzpoints {

const float kStopMin = 1.0f, kStopMax = 12.0f, kStopStep = 1.0f;      // the slider's stops (12 = the "999" stop)
const float kStopDefault = 11.0f;                                      // where the slider starts (the switch itself always starts OFF)
const int kBig = 999;                                                  // the last stop
const int kHighestSmall = 11;                                          // stops 1 .. 11 mean exactly that many points

// stop number (a float from the slider) -> points. Anything out of range is pulled in to the nearest stop.
inline int pointsForStop(float stop) {
    long s = std::lround(stop);
    if (s < 1) s = 1;
    if (s > 12) s = 12;
    return s <= kHighestSmall ? static_cast<int>(s) : kBig;
}

// points -> stop number (999 -> 12; anything above 11 counts as the last stop)
inline float stopForPoints(int points) {
    if (points < 1) return kStopMin;
    if (points > kHighestSmall) return kStopMax;
    return static_cast<float>(points);
}

inline std::string pointsText(int points) {
    char b[16];
    std::snprintf(b, sizeof b, "%d", points);
    return b;
}

}  // namespace tzpoints
