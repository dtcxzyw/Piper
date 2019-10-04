#pragma once
#pragma warning(push, 0)
#include "../ThirdParty/glm/glm/glm.hpp"
#include "../ThirdParty/glm/glm/gtc/constants.hpp"
#include "../ThirdParty/glm/glm/gtc/quaternion.hpp"
#include <optix.h>
#pragma warning(pop)

using Vec3 = glm::vec3;
using Vec2 = glm::vec2;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;
using Mat3 = glm::mat3;
using Quat = glm::quat;
using Uint2 = glm::uvec2;
using Spectrum = Vec3;
using Uint3 = glm::uvec3;

struct LightSample final {
    Vec3 wi;
    Spectrum rad;
};

constexpr unsigned geometryMask = 1 << 0;
constexpr unsigned lightVolumeMask = 1 << 1;
constexpr unsigned radianceRay = 0;
constexpr unsigned occlusionRay = 1;

enum class BxDFType {
    Reflection = 1,
    Transmission = 2,
    Specular = 4,
    Diffuse = 8,
    Glossy = 16,
    All = 31
};

enum class SBTSlot : unsigned {
    generateRay,
    samplePixel,
    // sampleOneLight,
    userOffset
};

struct SRT final {
    Vec3 scale;
    Quat rotate;
    Vec3 trans;
    Mat4 getPointTrans() const {
        Mat4 t = glm::translate(glm::identity<Mat4>(), trans);
        Mat4 r = glm::mat4_cast(rotate);
        return glm::scale(t * r, scale);
    }
};

struct LaunchParam final {
    unsigned samplerSbtOffset, lightSbtOffset;
    OptixTraversableHandle root;
};