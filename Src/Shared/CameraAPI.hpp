#pragma once
#include "PluginShared.hpp"

struct CameraData final {
    unsigned maxSampleDim, dss;
    OptixProgramGroup group;
};

class Camera : public Bus::ModuleFunctionBase {
protected:
    explicit Camera(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Camera:1";
    }

    virtual CameraData init(PluginHelper helper,
                            std::shared_ptr<Config> device) = 0;

    virtual Data setArgs(float focalLength, float fStop, float focalDistance,
                         Uint2 fullFilm, Vec3 pos, const Quat& posture) = 0;
};

inline float toFov(float focalLength, Vec2 sensor) {
    float sensorSize = hypotf(sensor.x, sensor.y);
    return 2.0f * atanf(sensorSize / (2.0f * focalLength));
}

inline float toFocalLength(float fov, Vec2 sensor) {
    float sensorSize = hypotf(sensor.x, sensor.y);
    return sensorSize / tanf(fov * 0.5f) * 0.5f;
}
