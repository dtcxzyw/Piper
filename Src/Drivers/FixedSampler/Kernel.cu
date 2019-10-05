#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

GLOBAL void __raygen__renderKernel() {
    const DataDesc* data =
        reinterpret_cast<DataDesc*>(optixGetSbtDataPointer());
    uint3 pixelPos = optixGetLaunchIndex();
    unsigned id = data->width * (data->sampleIdx * data->height + pixelPos.y) +
        pixelPos.x;
    Vec4 res = optixContinuationCall<Vec4, unsigned, Uint2>(
        static_cast<unsigned>(SBTSlot::samplePixel), id,
        Uint2{ pixelPos.x, pixelPos.y });
    if(data->filtBadColor &
       !(isfinite(res.x) & isfinite(res.y) & isfinite(res.z) & isfinite(res.w)))
        return;
    data->outputBuffer[data->width * pixelPos.y + pixelPos.x] += res;
}

GLOBAL void __miss__rad() {
    printf("miss\n");
}
GLOBAL void __miss__occ() {
    optixSetPayload_0(1);
}
GLOBAL void __exception__default() {
    int code = optixGetExceptionCode();
    if(code == OPTIX_EXCEPTION_CODE_TRAVERSAL_INVALID_HIT_SBT)
        printf("Invalid hit sbt idx=%d,off=%d\n",
               optixGetExceptionInvalidSbtOffset(), optixGetPrimitiveIndex());
    else
        printf("exception %d\n", code);
}
