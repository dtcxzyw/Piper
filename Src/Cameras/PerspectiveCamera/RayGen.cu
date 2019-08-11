#include "../../CUDA.hpp"

rtDeclareVariable(float3, cameraBase, , );
rtDeclareVariable(float3, cameraDown, , );
rtDeclareVariable(float3, cameraRight, , );
rtDeclareVariable(float3, cameraHole, , );
rtDeclareVariable(float3, cameraFStopX, , );
rtDeclareVariable(float3, cameraFStopY, , );

DEVICE void sampleRay(uint2 pixel, RaySample &sample, uint32 &seed) {
    ++seed;
    float2 pixelPos = { sample1(seed)*pixel.x, sample2(seed)*pixel.y };
    sample.ori = cameraBase + cameraRight * pixelPos.x +
        cameraDown * pixelPos.y;
    float angle = twoPi * sample3(seed);
    float radius = sqrtf(sample4(seed));
    float3 holePos = cameraHole + radius * cosf(angle) * cameraFStopX +
        radius * sinf(angle) * cameraFStopY;
    sample.dir = normalize(holePos - sample.ori);
}
