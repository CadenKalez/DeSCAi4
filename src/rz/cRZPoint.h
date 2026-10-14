// cRZPoint — integer 2-D point (two consecutive int32), as parsed from "x,y" score tokens.
#pragma once
#include <cstdint>

struct cRZPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;
    friend bool operator==(const cRZPoint&, const cRZPoint&) = default;
};
static_assert(sizeof(cRZPoint) == 8);
