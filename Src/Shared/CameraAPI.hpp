#pragma once
#include "ConfigAPI.hpp"

class Camera : public Bus::ModuleFunctionBase {
protected:
    explicit Camera(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Camera:1";
    }

    virtual fs::path init(PluginHelper helper, std::shared_ptr<Config> device) = 0;

    virtual void setArgs(optix::Program prog, float focalLength, float fStop,
                         float focalDistance, uint2 fullFilm, Vec3 pos,
                         const Quaternion& posture) const = 0;
};

inline float toFov(float focalLength, Vec2 sensor) {
    float sensorSize = hypotf(sensor.x, sensor.y);
    return 2.0f * atanf(sensorSize / (2.0f * focalLength));
}

inline float toFocalLength(float fov, Vec2 sensor) {
    float sensorSize = hypotf(sensor.x, sensor.y);
    return sensorSize / tanf(fov * 0.5f) * 0.5f;
}
