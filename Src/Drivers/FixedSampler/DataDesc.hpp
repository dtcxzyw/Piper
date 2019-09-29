#pragma once
#include "../../Shared/Shared.hpp"

struct DriverData final {
    Vec4* outputBuffer;
    unsigned width, height, sampleIdx;
    bool filtBadColor;
};
