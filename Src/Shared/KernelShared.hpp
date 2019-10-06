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

extern "C" __constant__ LaunchParam launchParam;

template <unsigned dim>
INLINEDEVICE float sample(unsigned idx) {
    return optixDirectCall<float, unsigned>(launchParam.samplerSbtOffset + dim,
                                            idx);
}

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

struct RaySample final {
    Vec3 ori, dir;
};

struct Payload final {
    Vec3 ori, wi;
    Spectrum f, rad;
    uint32 index;
    bool hit;
};

INLINEDEVICE void* unpackPointer(uint32_t i0, uint32_t i1) {
    const uint64_t uptr = static_cast<uint64_t>(i0) << 32 | i1;
    return reinterpret_cast<void*>(uptr);
}

INLINEDEVICE void packPointer(void* ptr, uint32_t& i0, uint32_t& i1) {
    const uint64_t uptr = reinterpret_cast<uint64_t>(ptr);
    i0 = uptr >> 32;
    i1 = uptr & 0x00000000ffffffff;
}

INLINEDEVICE Payload* getPayload() {
    return reinterpret_cast<Payload*>(
        unpackPointer(optixGetPayload_0(), optixGetPayload_1()));
}

template <typename T>
INLINEDEVICE const T* getSBTData() {
    return reinterpret_cast<T*>(optixGetSbtDataPointer());
}
