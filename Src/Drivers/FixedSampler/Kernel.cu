#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

GLOBAL void __raygen__renderKernel() {
    const DriverData* data =
        reinterpret_cast<DriverData*>(optixGetSbtDataPointer());
    uint3 pixelPos = optixGetLaunchIndex();
    unsigned id = data->width * (data->sampleIdx * data->height + pixelPos.y) +
        pixelPos.x;
    Vec4 res = optixDirectCall<Vec4, unsigned, Uint2>(
        static_cast<unsigned>(SBTSlot::samplePixel), id,
        Uint2{ pixelPos.x, pixelPos.y });
    if(data->filtBadColor &
       !(isfinite(res.x) & isfinite(res.y) & isfinite(res.z) & isfinite(res.w)))
        return;
    data->outputBuffer[data->width * pixelPos.y + pixelPos.x] += res;
}

GLOBAL void __miss__rad() {}
GLOBAL void __miss__occ() {}
GLOBAL void __exception__empty() {}
