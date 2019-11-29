#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

GLOBAL void __raygen__renderKernel() {
    auto data = getSBTData<DataDesc>();
    uint3 pixelPos = optixGetLaunchIndex();
    Spectrum acc = {};
    unsigned count = 0;
    for(unsigned id = data->sampleIdxBeg; id < data->sampleIdxEnd; ++id) {
        SamplerInitResult initRes = initSampler(id, pixelPos.x, pixelPos.y);
        SamplerContext sampler;
        sampler.dim = 0, sampler.index = initRes.index;
        RaySample ray =
            generateRay(data->generateRay, initRes.px, initRes.py, sampler);
        Spectrum res = sampleOnePixel(data->sampleOnePixel, ray, &sampler);
        if(data->filtBadColor &
           !(isfinite(res.x) & isfinite(res.y) & isfinite(res.z)))
            continue;
        acc += res;
        ++count;
    }
    data->outputBuffer[data->width * pixelPos.y + pixelPos.x] +=
        Vec4(acc, static_cast<float>(count));
}

GLOBAL void __miss__rad() {}
GLOBAL void __miss__occ() {
    optixSetPayload_0(1);
}
GLOBAL void __exception__default() {
    optix_impl::optixDumpExceptionDetails();
}
GLOBAL void __exception__silence() {}
