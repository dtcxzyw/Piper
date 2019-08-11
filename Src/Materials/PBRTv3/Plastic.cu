#include "../../CUDA.hpp"
#include "BxDF.hpp"

rtDeclareVariable(Payload, payload, rtPayload, );
rtDeclareVariable(Vec2, texCoord, attribute texCoord, );
TextureSampler(float4) materialKd;
TextureSampler(float4) materialKs;
TextureSampler(float2) materialRoughness;

RT_PROGRAM void closestHit() {
    ShadingSpace ss = calcPayload();
    Spectrum Kd = make_float3(tex2D(materialKd, texCoord.x, texCoord.y));
    Spectrum Ks = make_float3(tex2D(materialKs, texCoord.x, texCoord.y));
    Vec2 roughness = tex2D(materialRoughness, texCoord.x, texCoord.y);
    LambertianReflection lr(Kd);
    MicrofacetReflection mr(Ks, roughness, FresnelDielectric(1.5f, 1.0f));
    uint32 seed = ++payload.index;
    bool choice = sample1(seed) < 0.5f;
    Vec2 u = make_float2(sample2(seed), sample3(seed));
    Vec3 wi = choice ? lr.sampleF(ss.wo, u) : mr.sampleF(ss.wo, u);
    payload.f = (lr.f(ss.wo, wi) + mr.f(ss.wo, wi)) *
        (fabsf(dot(wi, ss.base.m_normal)) /
        (0.5f * (lr.pdf(ss.wo, wi) + mr.pdf(ss.wo, wi))));
    payload.wi = wi;
    ss.base.inverse_transform(payload.wi);
    LightSample ls = sampleOneLight();
    wi = ss.toLocal(ls.wi);
    payload.rad = ls.rad * (lr.f(ss.wo, wi) + mr.f(ss.wo, wi)) *
        (fabsf(dot(wi, ss.base.m_normal)) /
        (0.5f * (lr.pdf(ss.wo, wi) + mr.pdf(ss.wo, wi))));
}
