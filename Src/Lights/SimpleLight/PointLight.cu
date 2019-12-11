#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE LightSample __continuation_callable__sample(const Vec3& pos,
                                                   float rayTime,
                                                   SamplerContext& sampler) {
    auto light = getSBTData<PointLightData>();
    Vec3 diff = light->pos - pos;
    unsigned noHit = 0;
    optixTrace(
        launchParam.root, v2f(pos), v2f(diff), eps, oneMinusEps, rayTime, 255,
        OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT |
            OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
        occlusionOffset, traceSBTStride, occlusionMiss, noHit);
    LightSample res;
    float invSqrDis = 1.0f / dot(diff, diff);
    res.rad = (noHit ? light->lum * invSqrDis : Spectrum{ 0.0f });
    res.wi = diff * sqrt(invSqrDis);
    return res;
}

void check(LightSampleFunction = __continuation_callable__sample);
