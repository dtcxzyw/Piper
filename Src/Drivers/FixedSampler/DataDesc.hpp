#pragma once
#include "../../Shared/Shared.hpp"

struct DataDesc final {
    Vec4* outputBuffer;
    unsigned width, height, sampleIdxBeg, sampleIdxEnd, sampleOnePixel,
        generateRay;
    bool filtBadColor;
};
