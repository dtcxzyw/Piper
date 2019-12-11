#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE LightSample __continuation_callable__sample(const Vec3& pos,
                                                   float rayTime,
                                                   SamplerContext& sampler) {
    auto light = getSBTData<SpotLightData>();
    Vec3 diff = light->pos - pos;
    float invSqrDis = 1.0f / dot(diff, diff);
    float invDis = sqrt(invSqrDis);
    float angle = dot(diff, light->negSpotDir) * invDis;
    if(angle <= light->outerCutOff) {
        LightSample res;
        res.rad = Spectrum{ 0.0f };
        return res;
    }
    unsigned noHit = 0;
    optixTrace(
        launchParam.root, v2f(pos), v2f(diff), eps, oneMinusEps, rayTime, 255,
        OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT |
            OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
        occlusionOffset, traceSBTStride, occlusionMiss, noHit);
    LightSample res;
    if(noHit) {
        res.rad = light->lum *
            (invSqrDis *
             (light->invDelta ?
                  fminf(1.0f, (angle - light->outerCutOff) * light->invDelta) :
                  1.0f));
    } else
        res.rad = Spectrum{ 0.0f };
    res.wi = diff * invDis;
    return res;
}

void check(LightSampleFunction = __continuation_callable__sample);
