#include "../../Shared/ConfigAPI.hpp"
#include "../../Shared/LightAPI.hpp"
#include "DataDesc.hpp"
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinLight.SimpleLight");

class DirectionalLight final : public Light {
private:
    ProgramGroup mProgramGroup;
    LightData mData;

public:
    explicit DirectionalLight(Bus::ModuleInstance& instance)
        : Light(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> cfg) override {
        BUS_TRACE_BEG() {
            DirLight data;
            data.lum = cfg->attribute("Lum")->asVec3();
            data.negDir =
                glm::normalize(-cfg->attribute("Direction")->asVec3());
            OptixModule mod = helper->loadModuleFromFile(
                modulePath().parent_path() / "DirLight.ptx");
            OptixProgramGroupDesc desc = {};
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.moduleCC = mod;
            desc.callables.entryFunctionNameCC =
                "__continuation_callable__sample";
            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mProgramGroup.reset(group);
            mData.sbtData = packSBTRecord(mProgramGroup.get(), data);
            mData.maxSampleDim = 0;
            mData.group = mProgramGroup.get();
            OptixStackSizes size;
            checkOptixError(optixProgramGroupGetStackSize(group, &size));
            mData.css = size.cssCC;
            mData.dss = 0;
        }
        BUS_TRACE_END();
    }
    LightData getData() override {
        return mData;
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
        res.guid = Bus::str2GUID("{5B55F22A-2BDB-43B6-8EED-0CE0FBB49949}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "SimpleLight";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Light::getInterface())
            return { "DirectionalLight" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "DirectionalLight")
            return std::make_shared<DirectionalLight>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
