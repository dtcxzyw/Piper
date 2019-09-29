#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE RaySample __direct_callable__sampleRay(Uint2 pixel, uint32& seed) {
    ++seed;
    Vec2 pixelPos = { sample<0>(seed) + pixel.x, sample<1>(seed) + pixel.y };
    const KernelData* data =
        reinterpret_cast<KernelData*>(optixGetSbtDataPointer());
    Vec3 ori = data->base + data->right * pixelPos.x + data->down * pixelPos.y;
    Vec3 pinHoleDir = data->hole - ori;
    Vec3 focalPoint =
        ori + pinHoleDir * (data->focal / dot(data->axis, pinHoleDir));
    float angle = glm::two_pi<float>() * sample<2>(seed);
    float radius = sqrtf(sample<3>(seed));
    RaySample res;
    res.ori = data->hole + radius * cosf(angle) * data->fStopX +
        radius * sinf(angle) * data->fStopY;
    res.dir = normalize(focalPoint - res.ori);
    return res;
}
