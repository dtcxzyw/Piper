#pragma once
#pragma warning(push,0)
#define NOMINMAX
#include <optixu/optixu_math.h>
#include <optixu/optixu_matrix.h>
#include <optixu/optixu_quaternion_namespace.h>
#undef ERROR
#pragma warning(pop)

using Vec3 = float3;
using Vec2 = float2;
using Index = uint3;
using Mat4 = optix::Matrix4x4;
using Mat3 = optix::Matrix3x3;
using optix::Quaternion;
using Spectrum = Vec3;//CIE XYZ D65

struct Vertex final {
    Vec3 pos;
    Vec3 normal;
    Vec3 tangent;
    Vec2 texCoord;
    float padding[2];
};

struct LightSample final {
    Vec3 wi;
    Spectrum rad;
};

constexpr unsigned geometryMask = 1 << 0;
constexpr unsigned lightVolumeMask = 1 << 1;
