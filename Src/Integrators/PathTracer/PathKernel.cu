#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE Spectrum __continuation_callable__traceKernel(RaySample ray,
                                                     SamplerContext* sampler) {
    Spectrum res{ 0.0f };
    auto data = getSBTData<DataDesc>();
    Payload payload;
    payload.sampler = sampler;
    uint32_t p0, p1;
    packPointer(&payload, p0, p1);
    Spectrum att{ 1.0f };
    for(unsigned i = 0; i < data->maxDepth; ++i) {
        payload.hit = false;
        payload.f = Spectrum{ 0.0f };
        payload.rad = Spectrum{ 0.0f };
        // Russian roulette
        if(i > 3) {
            float q = fmax(0.05f,
                           1.0f -
                               (0.212671f * att.r + 0.715160f * att.g +
                                0.072169f * att.b));
            if((*sampler)() < q)
                break;
            att /= 1.0 - q;
        }
        // TODO:rayTime
        optixTrace(launchParam.root, v2f(ray.ori), v2f(ray.dir), eps, 1e20f,
                   0.0f, 255, OPTIX_RAY_FLAG_NONE, radianceOffset,
                   traceSBTStride, radianceMiss, p0, p1);
        res += att * payload.rad;
        if(!payload.hit)
            break;
        att *= payload.f;
        ray.ori = payload.ori;
        ray.dir = payload.wi;
    }
    return res;
}

void check(PixelSampleFunction = __continuation_callable__traceKernel) {}
