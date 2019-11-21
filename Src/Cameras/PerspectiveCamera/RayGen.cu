#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE RaySample __direct_callable__sampleRay(float px, float py,
                                              SamplerContext& sampler) {
    auto data = getSBTData<DataDesc>();
    Vec3 ori = data->base + data->right * px + data->down * py;
    Vec3 pinHoleDir = data->hole - ori;
    Vec3 focalPoint =
        ori + pinHoleDir * (data->focal / dot(data->axis, pinHoleDir));
    float angle = glm::two_pi<float>() * sampler();
    float radius = sqrtf(sampler());
    RaySample res;
    res.ori = data->hole + radius * cosf(angle) * data->fStopX +
        radius * sinf(angle) * data->fStopY;
    res.dir = normalize(focalPoint - res.ori);
    return res;
}

void check(RayGenerateFunction = __direct_callable__sampleRay);
