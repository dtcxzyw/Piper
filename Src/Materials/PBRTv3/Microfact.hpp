#pragma once
#include "../../Shared/KernelShared.hpp"

namespace TrowbridgeReitzDistribution {
    DEVICE float roughnessToAlpha(float roughness);
    DEVICE float D(const Vec3& wh, const Vec2& alpha);
    DEVICE float lambda(const Vec3& w, const Vec2& alpha);
    DEVICE Vec3 sampleWh(const Vec3& wo, const Vec2& alpha, const Vec2& u);
    DEVICE float pdf(const Vec3& wo, const Vec3& wh, const Vec2& alpha);
    DEVICE float G(const Vec3& wo, const Vec3& wi, const Vec2& alpha);
}  // namespace TrowbridgeReitzDistribution
