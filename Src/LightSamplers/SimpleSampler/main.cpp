#include "../../Shared/LightSamplerAPI.hpp"
#include "DataDesc.hpp"
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinLightSampler.SimpleSampler");

class UniformSampler final : public LightSampler {
private:
    Module mProg;
    ProgramGroup mProgramGroup;

public:
    explicit UniformSampler(Bus::ModuleInstance& instance)
        : LightSampler(instance) {}
    LightSamplerData init(PluginHelper helper, std::shared_ptr<Config> cfg,
                          size_t lightNum) override {
        BUS_TRACE_BEG() {
            DataDesc data;
            data.lightNum = static_cast<unsigned>(lightNum);
            mProg =
                helper->compileFile(modulePath().parent_path() / "Sampler.ptx");
            OptixProgramGroupDesc desc = {};
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.moduleCC = mProg.get();
            desc.callables.entryFunctionNameCC =
                "__continuation_callable__sample";
            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mProgramGroup.reset(group);
            LightSamplerData res;
            res.sbtData = packSBTRecord(group, data);
            res.maxSampleDim = 1;
            res.group = group;
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
        res.guid = Bus::str2GUID("{11E4DC1D-3874-44F6-8C16-5E7559552D16}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "SimpleSampler";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == LightSampler::getInterface())
            return { "UniformSampler" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "UniformSampler")
            return std::make_shared<UniformSampler>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
