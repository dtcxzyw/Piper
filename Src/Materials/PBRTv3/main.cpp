#include "../../Shared/ConfigAPI.hpp"
#include "../../Shared/MaterialAPI.hpp"
#include "../../Shared/TextureSamplerAPI.hpp"
#include "DataDesc.hpp"
#pragma warning(push, 0)
#define NOMINMAX
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinMaterial.PBRTv3");

unsigned loadTexture(std::shared_ptr<Config> cfg, PluginHelper helper,
                     const char* name, std::shared_ptr<TextureSampler>& inst,
                     unsigned& dss) {
    BUS_TRACE_BEG() {
        if(!cfg->hasAttr(name))
            return 0;
        cfg = cfg->attribute(name);
        inst = helper->instantiateAsset<TextureSampler>(cfg);
        TextureSamplerData data = inst->getData();
        dss = std::max(dss, data.dss);
        return helper->addCallable(data.group, data.sbtData);
    }
    BUS_TRACE_END();
}

class Plastic final : public Material {
private:
    ProgramGroup mGroup;
    std::shared_ptr<TextureSampler> mKd, mKs, mRoughness;
    MaterialData mData;

public:
    explicit Plastic(Bus::ModuleInstance& instance) : Material(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            mData.dss = 0;
            PlasticData data;
            data.kd = loadTexture(config, helper, "Diffuse", mKd, mData.dss);
            data.ks = loadTexture(config, helper, "Specular", mKs, mData.dss);
            data.roughness =
                loadTexture(config, helper, "Roughness", mRoughness, mData.dss);
            OptixModule mod = helper->loadModuleFromFile(
                modulePath().parent_path() / "Plastic.ptx");
            OptixProgramGroupDesc desc = {};
            desc.flags = 0;
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.entryFunctionNameCC =
                "__continuation_callable__sample";
            desc.callables.moduleCC = mod;
            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mGroup.reset(group);
            mData.group = group;
            mData.maxSampleDim = 3;
            mData.radData = packSBTRecord(group, data);
            OptixStackSizes size;
            checkOptixError(optixProgramGroupGetStackSize(group, &size));
            mData.css = size.cssCC;
        }
        BUS_TRACE_END();
    }
    MaterialData getData() override {
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
        res.guid = Bus::str2GUID("{022757A7-3D5C-470C-A056-67A00D3EADD0}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "PBRTv3";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Material::getInterface())
            return { "Plastic" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "Plastic")
            return std::make_shared<Plastic>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
