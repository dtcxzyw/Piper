#include "../../Shared/KernelShared.hpp"

rtDeclareVariable(float3, cameraBase, , );
rtDeclareVariable(float3, cameraDown, , );
rtDeclareVariable(float3, cameraRight, , );
rtDeclareVariable(float3, cameraHole, , );
rtDeclareVariable(float3, cameraFStopX, , );
rtDeclareVariable(float3, cameraFStopY, , );
rtDeclareVariable(float, cameraFocal, , );
rtDeclareVariable(float3, cameraAxis, , );

DEVICE void sampleRay(uint2 pixel, RaySample &sample, uint32 &seed) {
    ++seed;
    Vec2 pixelPos = { sample1(seed) + pixel.x, sample2(seed) + pixel.y };
    Vec3 ori = cameraBase + cameraRight * pixelPos.x + cameraDown * pixelPos.y;
    Vec3 pinHoleDir = cameraHole - ori;
    Vec3 focalPoint = ori + pinHoleDir * (cameraFocal / dot(cameraAxis, pinHoleDir));
    float angle = twoPi * sample3(seed);
    float radius = sqrtf(sample4(seed));
    sample.ori = cameraHole + radius * cosf(angle) * cameraFStopX +
        radius * sinf(angle) * cameraFStopY;
    sample.dir = normalize(focalPoint - sample.ori);
}
