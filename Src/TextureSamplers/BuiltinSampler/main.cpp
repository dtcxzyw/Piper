#include "../../Shared/ConfigAPI.hpp"
#include "../../Shared/TextureSamplerAPI.hpp"
#include "DataDesc.hpp"
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinTextureSampler.BuiltinSampler");

class ConstantColor final : public TextureSampler {
private:
    ProgramGroup mProgramGroup;
    TextureSamplerData mData;

public:
    explicit ConstantColor(Bus::ModuleInstance& instance)
        : TextureSampler(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> cfg) override {
        BUS_TRACE_BEG() {
            Constant data;
            data.color = cfg->attribute("Color")->asVec3();
            OptixModule mod = helper->loadModuleFromFile(
                modulePath().parent_path() / "Constant.ptx");
            OptixProgramGroupDesc desc = {};
            desc.flags = 0;
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.moduleDC = mod;
            desc.callables.entryFunctionNameDC = "__direct_callable__tex";
            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mProgramGroup.reset(group);
            mData.sbtData = packSBTRecord(group, data);
            mData.group = group;
        }
        BUS_TRACE_END();
    }
    TextureSamplerData getData() override {
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
        res.guid = Bus::str2GUID("{F177FF52-7AB2-466D-B898-75B270E0FCB8}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "BuiltinTextureSampler";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == TextureSampler::getInterface())
            return { "Constant" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "Constant")
            return std::make_shared<ConstantColor>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
