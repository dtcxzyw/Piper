#pragma once
#include "Common.hpp"
#pragma warning(push,0)
#define RT_USE_TEMPLATED_RTCALLABLEPROGRAM
#include <optix_device.h>
#pragma warning(pop)

#define DEVICE __device__ 
#define CONSTANT static __constant__ __device__
#define INLINEDEVICE __inline__ DEVICE
#define TextureSampler(T) rtTextureSampler<T, cudaTextureType2D, cudaReadModeElementType>

constexpr float pi = M_PIf;
constexpr float twoPi = 2.0 * M_PIf;
constexpr float invPi = M_1_PIf;
constexpr float eps = 1e-8f;

using uint32=unsigned int;

DEVICE uint32 initSeed(uint32 index, uint32 mask);
DEVICE float sample1(uint32 idx);
DEVICE float sample2(uint32 idx);
DEVICE float sample3(uint32 idx);
DEVICE float sample4(uint32 idx);
DEVICE float sample5(uint32 idx);
DEVICE float sample6(uint32 idx);

template<typename T>
INLINEDEVICE void swap(T &a, T &b) {
    T c = a;
    a = b;
    b = c;
}

template<typename T>
INLINEDEVICE auto saturate(const T &x)->T {
    return clamp(x, 0.0f, 1.0f);
}

INLINEDEVICE float fixTri(float x) {
    return clamp(x, -1.0f, 1.0f);
}

INLINEDEVICE float cos2Sin(float x) {
    return sqrtf(fmaxf(0.0f, 1.0f - x * x));
}

INLINEDEVICE float sin2Cos(float x) {
    return cos2Sin(x);
}

INLINEDEVICE float cosTheta(const Vec3 &w) { return w.z; }
INLINEDEVICE float cos2Theta(const Vec3 &w) { return w.z * w.z; }
INLINEDEVICE float absCosTheta(const Vec3 &w) { return fabsf(w.z); }
INLINEDEVICE float sin2Theta(const Vec3 &w) {
    return fmaxf(0.0f, 1.0f - cos2Theta(w));
}

INLINEDEVICE float sinTheta(const Vec3 &w) { return sqrtf(sin2Theta(w)); }

INLINEDEVICE float tanTheta(const Vec3 &w) { return sinTheta(w) / cosTheta(w); }

INLINEDEVICE float tan2Theta(const Vec3 &w) {
    return sin2Theta(w) / cos2Theta(w);
}

INLINEDEVICE float cosPhi(const Vec3 &w) {
    float sinThetaV = sinTheta(w);
    return (sinThetaV <= eps) ? 1.0f : fixTri(w.x / sinThetaV);
}

INLINEDEVICE float sinPhi(const Vec3 &w) {
    float sinThetaV = sinTheta(w);
    return (sinThetaV <= eps) ? 0.0f : fixTri(w.y / sinThetaV);
}

INLINEDEVICE float sqr(float x) {
    return x * x;
}

INLINEDEVICE float cos2Phi(const Vec3 &w) { return sqr(cosPhi(w)); }

INLINEDEVICE float sin2Phi(const Vec3 &w) { return sqr(sinPhi(w)); }

INLINEDEVICE float cosDPhi(const Vec3 &wa, const Vec3 &wb) {
    return fixTri(
        (wa.x * wb.x + wa.y * wb.y) / sqrtf((wa.x * wa.x + wa.y * wa.y) *
        (wb.x * wb.x + wb.y * wb.y)));
}

INLINEDEVICE bool sameHemisphere(const Vec3 &a, const Vec3 &b) {
    return a.z * b.z > 0.0f;
}

INLINEDEVICE Vec3 sqrtf(const Vec3 &a) {
    return make_float3(sqrtf(a.x), sqrtf(a.y), sqrtf(a.z));
}

#define black make_float3(0.0f)

struct RaySample final {
    Vec3 ori;
    Vec3 dir;
};

class Light;
DEVICE LightSample sampleOneLight(const Vec3 &ori, float u);

struct ShadingSpace final {
    optix::Onb base;
    Vec3 wo;
    DEVICE ShadingSpace(const Vec3 &normal) :base(normal) {}
    DEVICE Vec3 toLocal(const Vec3 &dir) const {
        return make_float3(dot(dir, base.m_tangent),
            dot(dir, base.m_binormal),
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

struct PayloadShadow final {
    bool hit;
};
