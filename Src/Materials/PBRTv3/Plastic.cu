#include "../../Shared/KernelShared.hpp"
#include "BxDF.hpp"
#include "DataDesc.hpp"
#include "ShadingSpace.hpp"

DEVICE void __continuation_callable__sample(Payload* payload, Vec3 dir,
                                            Vec3 hit, Vec3 ng, Vec3 ns,
                                            Vec2 texCoord, float rayTime,
                                            bool front) {
    auto data = getSBTData<PlasticData>();
    Spectrum Kd = builtinTex2D(data->kd, texCoord);
    Spectrum Ks = builtinTex2D(data->ks, texCoord);
    Vec2 roughness = builtinTex2D(data->roughness, texCoord);

    Vec3 wo, frontOffset;
    Mat3 w2s, s2w;
    calcShadingSpace(dir, ng, ns, front, wo, frontOffset, w2s);
    s2w = glm::transpose(w2s);

    LambertianReflection lr(Kd);
    MicrofacetReflection mr(Ks, roughness, FresnelDielectric(1.5f, 1.0f));
    uint32 seed = ++payload->index;
    bool choice = sample<0>(seed) < 0.5f;
    Vec2 u = { sample<1>(seed), sample<2>(seed) };
    Vec3 wi = choice ? lr.sampleF(wo, u) : mr.sampleF(wo, u);
    float fpdf = 0.5f * (lr.pdf(wo, wi) + mr.pdf(wo, wi));
    if(fpdf >= eps)
        payload->f = (lr.f(wo, wi) + mr.f(wo, wi)) * (absCosTheta(wi) / fpdf);
    payload->wi = s2w * wi;
    LightSample ls = sampleOneLight(payload->ori, rayTime, payload->index);
    wi = w2s * ls.wi;
    float lpdf = 0.5f * (lr.pdf(wo, wi) + mr.pdf(wo, wi));
    if(lpdf >= eps)
        payload->rad = ls.rad * (lr.f(wo, wi) + mr.f(wo, wi)) *
            (absCosTheta(wi) / lpdf);
}
