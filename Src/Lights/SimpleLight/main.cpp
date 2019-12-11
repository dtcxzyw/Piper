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
            DirLightData data;
            data.lum = cfg->attribute("Lum")->asVec3();
            data.negDir =
                glm::normalize(-cfg->attribute("Direction")->asVec3());
            const ModuleDesc& mod =
                helper->getModuleManager()->getModuleFromFile(
                    modulePath().parent_path() / "DirLight.ptx");
            OptixProgramGroupDesc desc = {};
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.moduleCC = mod.handle.get();
            desc.callables.entryFunctionNameCC =
                mod.map("__continuation_callable__sample");
            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mProgramGroup.reset(group);
            mData.sbtData = packSBTRecord(mProgramGroup.get(), data);
            mData.maxSampleDim = 0;
            mData.group = group;
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

class PointLight final : public Light {
private:
    ProgramGroup mProgramGroup;
    LightData mData;

public:
    explicit PointLight(Bus::ModuleInstance& instance) : Light(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> cfg) override {
        BUS_TRACE_BEG() {
            PointLightData data;
            data.lum = cfg->attribute("Lum")->asVec3();
            data.pos = cfg->attribute("Position")->asVec3();
            const ModuleDesc& mod =
                helper->getModuleManager()->getModuleFromFile(
                    modulePath().parent_path() / "PointLight.ptx");
            OptixProgramGroupDesc desc = {};
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.moduleCC = mod.handle.get();
            desc.callables.entryFunctionNameCC =
                mod.map("__continuation_callable__sample");
            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mProgramGroup.reset(group);
            mData.sbtData = packSBTRecord(mProgramGroup.get(), data);
            mData.maxSampleDim = 0;
            mData.group = group;
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

class SpotLight final : public Light {
private:
    ProgramGroup mProgramGroup;
    LightData mData;

public:
    explicit SpotLight(Bus::ModuleInstance& instance) : Light(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> cfg) override {
        BUS_TRACE_BEG() {
            SpotLightData data;
            data.lum = cfg->attribute("Lum")->asVec3();
            data.pos = cfg->attribute("Position")->asVec3();
            data.negSpotDir =
                -glm::normalize(cfg->attribute("SpotDirection")->asVec3());
            float cutOff = cfg->attribute("CutOff")->asFloat();
            data.outerCutOff = cfg->getFloat("OuterCutOff", cutOff);
            cutOff = cosf(glm::radians(cutOff));
            data.outerCutOff = cosf(glm::radians(data.outerCutOff));
            if(data.outerCutOff > cutOff)
                BUS_TRACE_THROW(std::logic_error("Bad cutoff"));
            if(cutOff - data.outerCutOff < 1e-4f)
                data.invDelta = 0.0f;
            else
                data.invDelta = 1.0f / (cutOff - data.outerCutOff);
            const ModuleDesc& mod =
                helper->getModuleManager()->getModuleFromFile(
                    modulePath().parent_path() / "SpotLight.ptx");
            OptixProgramGroupDesc desc = {};
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.moduleCC = mod.handle.get();
            desc.callables.entryFunctionNameCC =
                mod.map("__continuation_callable__sample");
            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mProgramGroup.reset(group);
            mData.sbtData = packSBTRecord(mProgramGroup.get(), data);
            mData.maxSampleDim = 0;
            mData.group = group;
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

class ConstantEnvironment final : public EnvironmentLight {
private:
    ProgramGroup mProgramGroup;

public:
    explicit ConstantEnvironment(Bus::ModuleInstance& instance)
        : EnvironmentLight(instance) {}
    LightData init(PluginHelper helper, std::shared_ptr<Config> cfg) override {
        BUS_TRACE_BEG() {
            ConstantData data;
            data.lum = cfg->attribute("Lum")->asVec3();
            const ModuleDesc& mod =
                helper->getModuleManager()->getModuleFromFile(
                    modulePath().parent_path() / "Constant.ptx");
            OptixProgramGroupDesc desc = {};
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
            desc.miss.module = mod.handle.get();
            desc.miss.entryFunctionName = mod.map("__miss__rad");
            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mProgramGroup.reset(group);
            LightData res;
            res.sbtData = packSBTRecord(group, data);
            res.maxSampleDim = 0;
            res.group = group;
            OptixStackSizes size;
            checkOptixError(optixProgramGroupGetStackSize(group, &size));
            res.css = size.cssMS;
            res.dss = 0;
            return res;
        }
        BUS_TRACE_END();
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
            return { "DirectionalLight", "PointLight", "SpotLight" };
        if(api == EnvironmentLight::getInterface())
            return { "ConstantEnvironment" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "DirectionalLight")
            return std::make_shared<DirectionalLight>(*this);
        if(name == "PointLight")
            return std::make_shared<PointLight>(*this);
        if(name == "SpotLight")
            return std::make_shared<SpotLight>(*this);
        if(name == "ConstantEnvironment")
            return std::make_shared<ConstantEnvironment>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
