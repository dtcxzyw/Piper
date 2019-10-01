#pragma once
#include "../../Shared/Shared.hpp"

struct DataDesc final {
    const Vec3* vertex;
    const Uint3* index;
    const Vec3* normal;
    const Vec2* texCoord;
};
