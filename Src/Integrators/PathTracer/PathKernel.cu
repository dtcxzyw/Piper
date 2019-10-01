#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE Vec4 __continuation_callable__traceKernel(uint32 id, Uint2 pixelPos) {
    Spectrum res = Spectrum{ 0.0f };
    const DataDesc* data =
        reinterpret_cast<DataDesc*>(optixGetSbtDataPointer());
    Payload payload;
    uint32_t p0, p1;
    packPointer(&payload, p0, p1);
    for(unsigned i = 0; i < data->sample; ++i) {
        unsigned seed = ((id * data->sample + i) ^ 0xc3fea875 + i) ^ id;
        RaySample ray = optixDirectCall<RaySample, Uint2, unsigned&>(
            static_cast<unsigned>(SBTSlot::generateRay), pixelPos, seed);
        Spectrum att{ 1.0f };
        for(unsigned j = 0; j < data->maxDepth; ++j) {
            payload.index = seed;
            payload.hit = false;
            payload.f = Spectrum{ 0.0f };
            payload.rad = Spectrum{ 0.0f };
            optixTrace(launchParam.root, v2f(ray.ori), v2f(ray.dir), eps, 1e20f,
                       radianceRay, OPTIX_RAY_FLAG_NONE, radianceOffset,
                       traceSBTStride, radianceMiss, p0, p1);
            seed = payload.index;
            res += att * payload.rad;
            if(!payload.hit)
                break;
            att *= payload.f;
            ray.ori = payload.ori;
            ray.dir = payload.wi;
        }
    }
    return Vec4{ res, static_cast<float>(data->sample) };
}
