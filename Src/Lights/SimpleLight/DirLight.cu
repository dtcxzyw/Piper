#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE LightSample __continuation_callable__sample(const Vec3& pos,
                                                   const Mat4&) {
    const DirLight* light =
        reinterpret_cast<DirLight*>(optixGetSbtDataPointer());
    Vec3 ori = pos - light->distance * light->direction;
    unsigned hit = 0;
    optixTrace(launchParam.root, v2f(ori), v2f(light->direction), eps,
               light->distance, occlusionRay,
               OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, occlusionOffset,
               traceSBTStride, occlusionMiss, hit);
    LightSample res;
    res.rad = (hit ? Spectrum{ 0.0f } : light->lum);
    res.wi = light->direction;
    return res;
}
