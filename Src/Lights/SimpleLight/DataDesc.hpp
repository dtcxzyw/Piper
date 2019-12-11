#pragma once
#include "../../Shared/Shared.hpp"

struct DirLightData final {
    Spectrum lum;
    Vec3 negDir;
};

struct PointLightData final {
    Spectrum lum;
    Vec3 pos;
};

struct SpotLightData final {
    Spectrum lum;
    Vec3 pos, negSpotDir;
    float outerCutOff, invDelta;
};

struct ConstantData final {
    Spectrum lum;
};
