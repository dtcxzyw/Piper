#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE LightSample __continuation_callable__sample(const Vec3& pos,
                                                   float rayTime,
                                                   SamplerContext& sampler) {
    auto light = getSBTData<DirLight>();
    Vec3 ori = pos - light->distance * light->direction;
    unsigned noHit = 0;
    optixTrace(launchParam.root, v2f(ori), v2f(light->direction), eps,
               light->distance - eps, rayTime, geometryMask,
               OPTIX_RAY_FLAG_NONE, occlusionOffset, traceSBTStride,
               occlusionMiss, noHit);
    LightSample res;
    res.rad = (noHit ? light->lum : Spectrum{ 0.0f });
    res.wi = -light->direction;
    return res;
}
