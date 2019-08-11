#pragma once
#include "../../Common.hpp"

struct DirLight final {
    Spectrum lum;
    Vec3 direction;
    float distance;
};
