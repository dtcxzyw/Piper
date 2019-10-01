#include "../../Shared/CameraAPI.hpp"
#include "DataDesc.hpp"
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinCamera.PerspectiveCamera");

class PerspectiveCamera final : public Camera {
private:
    Module mModule;
    ProgramGroup mGroup;
    Vec2 mSensor;

public:
    explicit PerspectiveCamera(Bus::ModuleInstance& instance)
        : Camera(instance) {}
    CameraData init(PluginHelper helper,
                    std::shared_ptr<Config> device) override {
        BUS_TRACE_BEG() {
            mSensor = device->attribute("Sensor")->asVec2();
            mSensor /= 1e3f;
            mModule =
                helper->compileFile(modulePath().parent_path() / "RayGen.ptx");
            OptixProgramGroupDesc desc;
            desc.flags = 0;
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.moduleDC = mModule.get();
            desc.callables.entryFunctionNameDC = "__direct_callable__sampleRay";
            OptixProgramGroupOptions opt;
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mGroup.reset(group);
            CameraData res;
            res.maxSampleDim = 4U;
            res.group = mGroup.get();
            return res;
        }
        BUS_TRACE_END();
    }
    Data setArgs(float focalLength, float fStop, float focalDistance,
                 Uint2 fullFilm, Vec3 pos, const Quat& posture) const override {
        KernelData data;
        focalLength /= 1e3f;
        float aperture = focalLength / fStop * 0.5f;
        Vec2 pixelSize = mSensor / static_cast<Vec2>(fullFilm);
        Vec3 begOffset =
            posture * Vec3{ 0.5f * mSensor.x, -0.5f * mSensor.y, 0.0f };
        data.right = posture * Vec3{ -pixelSize.x, 0.0f, 0.0f };
        data.down = posture * Vec3{ 0.0f, pixelSize.y, 0.0f };
        data.base = pos + begOffset + posture * Vec3{ 0.0f, 0.0f, focalLength };
        data.fStopX = posture * Vec3{ aperture, 0.0f, 0.0f };
        data.fStopY = posture * Vec3{ 0.0f, aperture, 0.0f };
        data.hole = pos;
        data.focal = focalDistance + focalLength;
        data.axis = posture * Vec3{ 0.0f, 0.0f, -1.0f };
        return packSBT(mGroup.get(), data);
    }
};

class Instance final : public Bus::ModuleInstance {
public:
    Instance(const fs::path& path, Bus::ModuleSystem& sys)
        : Bus::ModuleInstance(path, sys) {
        checkOptixError(optixInit());
    }
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = BUS_DEFAULT_MODULE_NAME;
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
