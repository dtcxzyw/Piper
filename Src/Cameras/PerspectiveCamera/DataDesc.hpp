#pragma once
#include "../../Shared/Shared.hpp"

struct KernelData final {
    Vec3 base, down, right, hole, fStopX, fStopY, axis;
    float focal;
};
