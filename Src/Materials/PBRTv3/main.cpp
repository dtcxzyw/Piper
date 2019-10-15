#include "../../Shared/MaterialAPI.hpp"
#include "../../Shared/TextureSamplerAPI.hpp"
#include "DataDesc.hpp"
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinMaterial.PBRTv3");

unsigned loadTexture(Bus::ModuleSystem& sys, std::shared_ptr<Config> cfg,
                     PluginHelper helper, const char* name,
                     std::shared_ptr<TextureSampler>& inst) {
    BUS_TRACE_BEG() {
        if(!cfg->hasAttr(name))
            return 0;
        cfg = cfg->attribute(name);
        std::string type = cfg->attribute("Plugin")->asString();
        inst = sys.instantiateByName<TextureSampler>(type);
        TextureSamplerData data = inst->init(helper, cfg);
        return helper->addCallable(data.group, data.sbtData);
    }
    BUS_TRACE_END();
}

class Plastic final : public Material {
private:
    Module mModule;
    ProgramGroup mGroup;
    std::shared_ptr<TextureSampler> mKd, mKs, mRoughness;

public:
    explicit Plastic(Bus::ModuleInstance& instance) : Material(instance) {}
    MaterialData init(PluginHelper helper,
                      std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            PlasticData data;
            data.kd =
                loadTexture(this->system(), config, helper, "Diffuse", mKd);
            data.ks =
                loadTexture(this->system(), config, helper, "Specular", mKs);
            data.roughness = loadTexture(this->system(), config, helper,
                                         "Roughness", mRoughness);
            mModule =
                helper->compileFile(modulePath().parent_path() / "Plastic.ptx");
            OptixProgramGroupDesc desc = {};
            desc.flags = 0;
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.entryFunctionNameCC =
                "__continuation_callable__sample";
            desc.callables.moduleCC = mModule.get();
            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mGroup.reset(group);
            MaterialData res;
            res.group = group;
            res.maxSampleDim = 3;
            res.radData = packSBTRecord(group, data);
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
