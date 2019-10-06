#pragma once
#include "../../Shared/Shared.hpp"

struct DataDesc final {
    Vec3 base, down, right, hole, fStopX, fStopY, axis;
    float focal;
};
