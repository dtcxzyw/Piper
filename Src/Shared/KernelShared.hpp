#pragma once
#define __CUDACC__
#include "Shared.hpp"
#pragma warning(push, 0)
#include <cuda_device_runtime_api.h>
#pragma warning(pop)

#define DEVICE extern "C" __device__
#define GLOBAL extern "C" __global__
#define CONSTANT static __constant__ __device__
#define INLINEDEVICE __inline__ __device__

constexpr float eps = 1e-8f;

constexpr unsigned radianceMiss = 0;
constexpr unsigned occlusionMiss = 1;
constexpr unsigned radianceOffset = 0;
constexpr unsigned occlusionOffset = 1;
constexpr unsigned traceSBTStride = 2;

using uint32 = unsigned int;

DEVICE uint32 initSeed(uint32 index, uint32 mask);
DEVICE float sample1(uint32 idx);
DEVICE float sample2(uint32 idx);
DEVICE float sample3(uint32 idx);
DEVICE float sample4(uint32 idx);
DEVICE float sample5(uint32 idx);
DEVICE float sample6(uint32 idx);

INLINEDEVICE float3 v2f(const Vec3& v) {
    return make_float3(v.x, v.y, v.z);
}
INLINEDEVICE Vec3 f2v(const float3& v) {
    return Vec3{ v.x, v.y, v.z };
}

template <typename T>
INLINEDEVICE void swap(T& a, T& b) {
    T c = a;
    a = b;
    b = c;
}

template <typename T>
INLINEDEVICE auto saturate(const T& x) -> T {
    return glm::clamp(x, 0.0f, 1.0f);
}

INLINEDEVICE float fixTri(float x) {
    return glm::clamp(x, -1.0f, 1.0f);
}

INLINEDEVICE float cos2Sin(float x) {
    return sqrtf(fmaxf(0.0f, 1.0f - x * x));
}

INLINEDEVICE float sin2Cos(float x) {
    return cos2Sin(x);
}

INLINEDEVICE float cosTheta(const Vec3& w) {
    return w.z;
}
INLINEDEVICE float cos2Theta(const Vec3& w) {
    return w.z * w.z;
}
INLINEDEVICE float absCosTheta(const Vec3& w) {
    return fabsf(w.z);
}
INLINEDEVICE float sin2Theta(const Vec3& w) {
    return fmaxf(0.0f, 1.0f - cos2Theta(w));
}

INLINEDEVICE float sinTheta(const Vec3& w) {
    return sqrtf(sin2Theta(w));
}

INLINEDEVICE float tanTheta(const Vec3& w) {
    return sinTheta(w) / cosTheta(w);
}

INLINEDEVICE float tan2Theta(const Vec3& w) {
    return sin2Theta(w) / cos2Theta(w);
}

INLINEDEVICE float cosPhi(const Vec3& w) {
    float sinThetaV = sinTheta(w);
    return (sinThetaV <= eps) ? 1.0f : fixTri(w.x / sinThetaV);
}

INLINEDEVICE float sinPhi(const Vec3& w) {
    float sinThetaV = sinTheta(w);
    return (sinThetaV <= eps) ? 0.0f : fixTri(w.y / sinThetaV);
}

INLINEDEVICE float sqr(float x) {
    return x * x;
}

INLINEDEVICE float cos2Phi(const Vec3& w) {
    return sqr(cosPhi(w));
}

INLINEDEVICE float sin2Phi(const Vec3& w) {
    return sqr(sinPhi(w));
}

INLINEDEVICE float cosDPhi(const Vec3& wa, const Vec3& wb) {
    return fixTri(
        (wa.x * wb.x + wa.y * wb.y) /
        sqrtf((wa.x * wa.x + wa.y * wa.y) * (wb.x * wb.x + wb.y * wb.y)));
}

INLINEDEVICE bool sameHemisphere(const Vec3& a, const Vec3& b) {
    return a.z * b.z > 0.0f;
}

INLINEDEVICE Vec3 sqrtf(const Vec3& a) {
    return { sqrtf(a.x), sqrtf(a.y), sqrtf(a.z) };
}

#define black            \
    Spectrum {           \
        0.0f, 0.0f, 0.0f \
    }

struct RaySample final {
    Vec3 ori;
    Vec3 dir;
};

class Light;
DEVICE LightSample sampleOneLight(const Vec3& ori, float u);

/*
struct ShadingSpace final {
    optix::Onb base;
    Vec3 wo;
    DEVICE ShadingSpace(const Vec3& normal) : base(normal) {}
    DEVICE Vec3 toLocal(const Vec3& dir) const {
        return make_float3(dot(dir, base.m_tangent), dot(dir, base.m_binormal),
                           dot(dir, base.m_normal));
    }
};

struct Payload final {
    Vec3 ori;
    Vec3 wi;
    Spectrum f, rad;
    uint32 index;
    bool hit;
};

DEVICE ShadingSpace calcPayload(Payload& payload);
*/
