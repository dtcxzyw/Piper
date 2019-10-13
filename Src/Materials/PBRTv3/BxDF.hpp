#pragma once
#include "../../Shared/KernelShared.hpp"
#include "Fresnel.hpp"
#include "Microfact.hpp"

INLINEDEVICE Vec2 concentricSampleDisk(const Vec2& u) {
    // Map uniform random numbers to $[-1,1]^2$
    Vec2 uOffset = 2.0f * u - Vec2(1.0f);

    // Handle degeneracy at the origin
    if(uOffset.x == 0.0f && uOffset.y == 0.0f)
        return Vec2{ 0.0f };

    // Apply concentric mapping to point
    float theta, r;
    if(fabsf(uOffset.x) > fabsf(uOffset.y)) {
        r = uOffset.x;
        theta = glm::quarter_pi<float>() * (uOffset.y / uOffset.x);
    } else {
        r = uOffset.y;
        theta = glm::half_pi<float>() -
            glm::quarter_pi<float>() * (uOffset.x / uOffset.y);
    }
    return r * Vec2(cosf(theta), sinf(theta));
}

INLINEDEVICE Vec3 cosineSampleHemisphere(const Vec2& u) {
    Vec2 d = concentricSampleDisk(u);
    float z = sqrtf(fmaxf(0.0f, 1.0f - d.x * d.x - d.y * d.y));
    return Vec3(d.x, d.y, z);
}

class LambertianReflection final {
private:
    Spectrum mR;

public:
    INLINEDEVICE LambertianReflection(const Spectrum& r) : mR(r) {}
    INLINEDEVICE Spectrum f(const Vec3&, const Vec3&) const {
        return mR * glm::one_over_pi<float>();
    }
    INLINEDEVICE Vec3 sampleF(const Vec3& wo, const Vec2& u) const {
        Vec3 wi = cosineSampleHemisphere(u);
        wi.z = copysignf(wi.z, wo.z);
        return wi;
    }
    INLINEDEVICE float pdf(const Vec3& wo, const Vec3& wi) const {
        return sameHemisphere(wo, wi) ?
            absCosTheta(wi) * glm::one_over_pi<float>() :
            0.0f;
    }
};

class MicrofacetReflection final {
private:
    Spectrum mR;
    Vec2 mAlpha;
    Fresnel mFresnel;
    INLINEDEVICE Vec2 toAlpha(const Vec2& roughness) {
        using namespace TrowbridgeReitzDistribution;
        return Vec2{ roughnessToAlpha(roughness.x),
                     roughnessToAlpha(roughness.y) };
    }

public:
    INLINEDEVICE MicrofacetReflection(const Spectrum& r, const Vec2& roughness,
                                      const Fresnel& fresnel)
        : mR(r), mAlpha(toAlpha(roughness)), mFresnel(fresnel) {}

    INLINEDEVICE Spectrum f(const Vec3& wo, const Vec3& wi) const {
        float cosThetaO = absCosTheta(wo), cosThetaI = absCosTheta(wi);
        Vec3 wh = wi + wo;
        // Handle degenerate cases for microfacet reflection
        if(fminf(cosThetaI, cosThetaO) < eps)
            return Spectrum{ 0.0f };
        if(fmaxf(wh.x, fmaxf(wh.y, wh.z)) < eps)
            return Spectrum{ 0.0f };
        wh = normalize(wh);
        Spectrum F = mFresnel.eval(dot(wi, wh));
        return mR * F *
            (TrowbridgeReitzDistribution::D(wh, mAlpha) *
             TrowbridgeReitzDistribution::G(wo, wi, mAlpha) /
             (4.0f * cosThetaI * cosThetaO));
    }

    INLINEDEVICE Vec3 sampleF(const Vec3& wo, const Vec2& u) const {
        // Sample microfacet orientation $\wh$ and reflected direction $\wi$
        if(wo.z <= eps)
            return {};
        Vec3 wh = TrowbridgeReitzDistribution::sampleWh(wo, mAlpha, u);
        Vec3 wi = reflect(wo, wh);
        return wi;
    }
    INLINEDEVICE float pdf(const Vec3& wo, const Vec3& wi) const {
        if(!sameHemisphere(wo, wi))
            return 0.0f;
        Vec3 wh = normalize(wo + wi);
        return TrowbridgeReitzDistribution::pdf(wo, wh, mAlpha) /
            (4.0f * dot(wo, wh));
    }
};
