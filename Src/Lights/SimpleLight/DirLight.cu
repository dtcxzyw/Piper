#include "../../CUDA.hpp"
#include "DataDesc.hpp"

rtDeclareVariable(rtObject, globalTopNode, , );

RT_CALLABLE_PROGRAM LightSample sample(const Mat4 &,
    rtBufferId<char, 1> buf, const Vec3 &wi) {
    const DirLight *light = reinterpret_cast<DirLight *>(&buf[0]);
    Vec3 ori = light->distance * light->direction + wi;
    optix::Ray ray = optix::make_Ray(ori, light->direction, shadowRayType,
        0.0f, light->distance - eps);
    PayloadShadow shadow;
    shadow.hit = false;
    rtTrace(globalTopNode, ray, shadow, geometryMask);
    LightSample res;
    res.wi = light->direction;
    res.rad = (shadow.hit ? black : light->lum);
    return res;
}
