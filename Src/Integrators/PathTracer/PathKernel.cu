#include "../../CUDA.hpp"

rtDeclareVariable(rtObject, globalTopNode, , );
rtDeclareVariable(unsigned, integratorMaxDepth, , );
rtDeclareVariable(unsigned, integratorSample, , );
rtBuffer<float4, 2> driverOutputBuffer;
rtDeclareVariable(uint2, driverBegin, , );
rtDeclareVariable(unsigned, driverIndex, , );
rtDeclareVariable(int, driverFiltBadColor, , );
rtDeclareVariable(uint2, launchIndex, rtLaunchIndex, );

DEVICE void sampleRay(uint2 pixel, RaySample &sample, uint32 &seed);

RT_PROGRAM void traceKernel() {
    Spectrum res = black;
    uint2 pixelPos = launchIndex;
    optix::size_t2 filmSize = driverOutputBuffer.size();
    uint32 id = filmSize.x * (driverIndex * filmSize.y + pixelPos.y) + pixelPos.x;
    for (uint32 i = 0; i < integratorSample; ++i) {
        uint32 seed = initSeed(id * integratorSample + i, 0xc3fea875);
        optix::Ray curRay;
        {
            RaySample sample;
            sampleRay(pixelPos, sample, seed);
            curRay = optix::make_Ray(sample.ori, sample.dir, 0, 0.0f, RT_DEFAULT_MAX);
        }
        Spectrum att = make_float3(1.0f);
        for (uint32 i = 0; i < integratorMaxDepth; ++i) {
            Payload payload;
            payload.index = seed;
            payload.hit = false;
            payload.f = black;
            payload.rad = black;
            rtTrace(globalTopNode, curRay, payload, geometryMask | lightVolumeMask);
            seed = payload.index;
            res += att * payload.rad;
            att *= payload.f;
            if (!payload.hit)
                break;
        }
    }
    if (driverFiltBadColor & !(isfinite(res.x) & isfinite(res.y) & isfinite(res.z)))
        return;
    driverOutputBuffer[pixelPos] +=
        make_float4(res, static_cast<float>(integratorSample));
}
