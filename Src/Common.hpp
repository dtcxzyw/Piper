#pragma once
#pragma warning(push,0)
#define NOMINMAX
#include <optixu/optixu_math.h>
#include <optixu/optixu_matrix.h>
#include <optixu/optixu_quaternion_namespace.h>
#include <optixu/optixu_aabb_namespace.h>
#undef ERROR
#pragma warning(pop)

using Vec3 = float3;
using Vec2 = float2;
using Vec4 = float4;
using Mat4 = optix::Matrix4x4;
using Mat3 = optix::Matrix3x3;
using optix::Quaternion;
using Spectrum = Vec3;

struct LightSample final {
    Vec3 wi;
    Spectrum rad;
};

constexpr unsigned geometryMask = 1 << 0;
constexpr unsigned lightVolumeMask = 1 << 1;
constexpr unsigned radianceRayType = 0;
constexpr unsigned shadowRayType = 1;

enum class BxDFType {
    Reflection = 1,
    Transmission = 2,
    Specular = 4,
    Diffuse = 8,
    Glossy = 16,
    All = 31
};
