#pragma once
#include "../../Shared/Shared.hpp"

struct DirLight final {
    Spectrum lum;
    Vec3 negDir;
};
