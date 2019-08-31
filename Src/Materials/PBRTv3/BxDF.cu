#include "BxDF.hpp"
#include "Microfact.hpp"

DEVICE Spectrum LambertianReflection::f(const Vec3&, const Vec3&) const {
    return mR * invPi;
}

DEVICE Vec3 LambertianReflection::sampleF(const Vec3& wo, const Vec2& u) const {
    Vec3 wi;
    optix::cosine_sample_hemisphere(u.x, u.y, wi);
    wi.z = copysignf(wi.z, wo.z);
    return wi;
}

DEVICE float LambertianReflection::pdf(const Vec3& wo, const Vec3& wi) const {
    return sameHemisphere(wo, wi) ? absCosTheta(wi) * invPi : 0.0f;
}

DEVICE Spectrum MicrofacetReflection::f(const Vec3& wo, const Vec3& wi) const {
    float cosThetaO = absCosTheta(wo), cosThetaI = absCosTheta(wi);
    Vec3 wh = wi + wo;
    // Handle degenerate cases for microfacet reflection
    if(fminf(cosThetaI, cosThetaO) < eps)
        return black;
    if(fmaxf(wh) < eps)
        return black;
    wh = normalize(wh);
    Spectrum F = mFresnel.eval(dot(wi, wh));
    return mR * F *
        (TrowbridgeReitzDistribution::D(wh, mAlpha) *
         TrowbridgeReitzDistribution::G(wo, wi, mAlpha) /
         (4.0f * cosThetaI * cosThetaO));
}

DEVICE Vec3 MicrofacetReflection::sampleF(const Vec3& wo, const Vec2& u) const {
    // Sample microfacet orientation $\wh$ and reflected direction $\wi$
    // if (wo.z <= eps) return {};
    Vec3 wh = TrowbridgeReitzDistribution::sampleWh(wo, mAlpha, u);
    Vec3 wi = reflect(wo, wh);
    return wi;
}

DEVICE float MicrofacetReflection::pdf(const Vec3& wo, const Vec3& wi) const {
    if(!sameHemisphere(wo, wi))
        return 0.0f;
    Vec3 wh = normalize(wo + wi);
    return TrowbridgeReitzDistribution::pdf(wo, wh, mAlpha) /
        (4.0f * dot(wo, wh));
}

DEVICE Vec2 MicrofacetReflection::toAlpha(const Vec2& roughness) {
    using namespace TrowbridgeReitzDistribution;
    return make_float2(roughnessToAlpha(roughness.x),
                       roughnessToAlpha(roughness.y));
}
