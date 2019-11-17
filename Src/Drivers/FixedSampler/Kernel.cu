#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

GLOBAL void __raygen__renderKernel() {
    auto data = getSBTData<DataDesc>();
    uint3 pixelPos = optixGetLaunchIndex();
    Vec4 res = samplePixel(data->sampleIdx, pixelPos.x, pixelPos.y);
    if(data->filtBadColor &
       !(isfinite(res.x) & isfinite(res.y) & isfinite(res.z) & isfinite(res.w)))
        return;
    data->outputBuffer[data->width * pixelPos.y + pixelPos.x] += res;
}

GLOBAL void __miss__rad() {}
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
GLOBAL void __exception__silence() {}
