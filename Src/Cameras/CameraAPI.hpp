#pragma once
#include "../PluginShared.hpp"

class Camera : public PM::AbstractPlugin {
public:
    static std::string pluginInterface() {
        return "Piper.Camera:1";
    }

    static std::vector<std::string> pluginSearchPaths() {
        return { "Plugins/Cameras" };
    }

    explicit Camera(PM::AbstractManager &manager,
        const std::string &plugin)
        : AbstractPlugin{ manager, plugin } {}

    virtual fs::path init(PluginHelper helper, JsonHelper device, const fs::path &modulePath) = 0;

    virtual void setArgs(optix::Program prog, float focusLength, float fStop,
        uint2 fullFilm, Vec3 pos, const Quaternion &posture) const = 0;
};

inline float toFov(float focusLength, Vec2 sensor) {
    float sensorSize = hypotf(sensor.x, sensor.y);
    return 2.0f * atanf(sensorSize / (2.0f * focusLength));
}

inline float toFocusLength(float fov, Vec2 sensor) {
    float sensorSize = hypotf(sensor.x, sensor.y);
    return sensorSize / tanf(fov * 0.5f) * 0.5f;
}
