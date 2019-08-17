#define CORRADE_DYNAMIC_PLUGIN
#include "../CameraAPI.hpp"

class PerspectiveCamera final : public Camera {
private:
    Vec2 mSensor;
public:
    explicit PerspectiveCamera(PM::AbstractManager &manager,
        const std::string &plugin) : Camera{ manager, plugin } {}
    fs::path init(PluginHelper, JsonHelper device, const fs::path &modulePath) override {
        mSensor = device->toVec2("sensor");
        mSensor /= 1e3f;
        return modulePath / "RayGen.ptx";
    }
    void setArgs(optix::Program prog, float focalLength, float fStop,
        uint2 fullFilm, Vec3 pos, const Quaternion &posture) const override {
        focalLength /= 1e3f;
        fStop /= 1e3f;
        float2 pixelSize = mSensor / make_float2(fullFilm);
        Vec3 begOffset = posture * make_float3(0.5f * mSensor.x, -0.5f * mSensor.y, 0.0f);
        Vec3 right = posture * make_float3(-pixelSize.x, 0.0f, 0.0f);
        Vec3 down = posture * make_float3(0.0f, pixelSize.y, 0.0f);
        Vec3 base = pos + begOffset + posture * make_float3(0.0f, 0.0f, focalLength);
        Vec3 fStopX = posture * make_float3(fStop, 0.0f, 0.0f);
        Vec3 fStopY = posture * make_float3(0.0f, fStop, 0.0f);
        prog["cameraBase"]->setFloat(base);
        prog["cameraDown"]->setFloat(down);
        prog["cameraRight"]->setFloat(right);
        prog["cameraHole"]->setFloat(pos);
        prog["cameraFStopX"]->setFloat(fStopX);
        prog["cameraFStopY"]->setFloat(fStopY);
    }
};
CORRADE_PLUGIN_REGISTER(PerspectiveCamera, PerspectiveCamera,
    "Piper.Camera:1")
