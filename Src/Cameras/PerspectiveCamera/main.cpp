#include "../../Shared/CameraAPI.hpp"

class PerspectiveCamera final : public Camera {
private:
    Vec2 mSensor;

public:
    explicit PerspectiveCamera(Bus::ModuleInstance& instance)
        : Camera(instance) {}
    fs::path init(PluginHelper, std::shared_ptr<Config> device) override {
        mSensor = device->attribute("Sensor")->asVec2();
        mSensor /= 1e3f;
        return modulePath().parent_path() / "RayGen.ptx";
    }
    void setArgs(optix::Program prog, float focalLength, float fStop,
                 float focalDistance, uint2 fullFilm, Vec3 pos,
                 const Quaternion& posture) const override {
        focalLength /= 1e3f;
        float aperture = focalLength / fStop * 0.5f;
        float2 pixelSize = mSensor / make_float2(fullFilm);
        Vec3 begOffset =
            posture * make_float3(0.5f * mSensor.x, -0.5f * mSensor.y, 0.0f);
        Vec3 right = posture * make_float3(-pixelSize.x, 0.0f, 0.0f);
        Vec3 down = posture * make_float3(0.0f, pixelSize.y, 0.0f);
        Vec3 base =
            pos + begOffset + posture * make_float3(0.0f, 0.0f, focalLength);
        Vec3 fStopX = posture * make_float3(aperture, 0.0f, 0.0f);
        Vec3 fStopY = posture * make_float3(0.0f, aperture, 0.0f);
        prog["cameraBase"]->setFloat(base);
        prog["cameraDown"]->setFloat(down);
        prog["cameraRight"]->setFloat(right);
        prog["cameraHole"]->setFloat(pos);
        prog["cameraFStopX"]->setFloat(fStopX);
        prog["cameraFStopY"]->setFloat(fStopY);
        prog["cameraFocal"]->setFloat(focalDistance + focalLength);
        prog["cameraAxis"]->setFloat(posture * make_float3(0.0f, 0.0f, -1.0f));
    }
};

class Instance final : public Bus::ModuleInstance {
public:
    Instance(const fs::path& path, Bus::ModuleSystem& sys)
        : Bus::ModuleInstance(path, sys) {}
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = "Piper.BuiltinCamera.PerspectiveCamera";
        res.guid = Bus::str2GUID("{5E841908-9188-46AF-9272-F29F987B6C80}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "PerspectiveCamera";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Camera::getInterface())
            return { "PerspectiveCamera" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "PerspectiveCamera")
            return std::make_shared<PerspectiveCamera>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
