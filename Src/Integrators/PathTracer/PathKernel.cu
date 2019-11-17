#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE Vec4 __continuation_callable__traceKernel(unsigned sampleOffset,
                                                 unsigned x, unsigned y) {
    Spectrum res = Spectrum{ 0.0f };
    auto data = getSBTData<DataDesc>();
    Payload payload;
    uint32_t p0, p1;
    packPointer(&payload, p0, p1);
    for(unsigned i = 0; i < data->sample; ++i) {
        unsigned index = sampleOffset * data->sample + i;
        SamplerInitResult initRes = initSampler(index, x, y);
        SamplerContext sampler;
        sampler.dim = 0, sampler.index = initRes.index;
        RaySample ray = generateRay(initRes.px, initRes.py, sampler);
        Spectrum att{ 1.0f };
        for(unsigned j = 0; j < data->maxDepth; ++j) {
            payload.sampler = &sampler;
            payload.hit = false;
            payload.f = Spectrum{ 0.0f };
            payload.rad = Spectrum{ 0.0f };
            optixTrace(launchParam.root, v2f(ray.ori), v2f(ray.dir), eps, 1e20f,
                       0.0f, 255, OPTIX_RAY_FLAG_NONE, radianceOffset,
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
