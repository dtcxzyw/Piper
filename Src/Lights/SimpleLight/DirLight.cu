#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE LightSample __continuation_callable__sample(const Vec3& pos,
                                                   float rayTime,
                                                   SamplerContext& sampler) {
    auto light = getSBTData<DirLightData>();
    unsigned noHit = 0;
    optixTrace(launchParam.root, v2f(pos), v2f(light->negDir), eps, 1e20f,
               rayTime, 255,
               OPTIX_RAY_FLAG_DISABLE_ANYHIT |
                   OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT |
                   OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
               occlusionOffset, traceSBTStride, occlusionMiss, noHit);
    LightSample res;
    res.rad = (noHit ? light->lum : Spectrum{ 0.0f });
    res.wi = light->negDir;
    return res;
}

void check(LightSampleFunction = __continuation_callable__sample);
