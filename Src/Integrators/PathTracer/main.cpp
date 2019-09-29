#include "../../Shared/IntegratorAPI.hpp"
#include "DataDesc.hpp"
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinIntegrator.PathTracer");

class PathTracer final : public Integrator {
private:
    Module mModule;
    ProgramGroup mGroup;

public:
    explicit PathTracer(Bus::ModuleInstance& instance) : Integrator(instance) {}
    IntegratorData init(PluginHelper helper,
                        std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            mModule = helper->compileFile(modulePath().parent_path() /
                                          "PathKernel.ptx");
            OptixProgramGroupDesc desc;
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.flags = 0;
            desc.callables.entryFunctionNameCC =
                "__continuation_callable__traceKernel";
            desc.callables.moduleCC = mModule.get();
            OptixProgramGroupOptions opt;
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mGroup.reset(group);
            DataDesc data;
            data.maxDepth = config->attribute("MaxDepth")->asUint();
            data.sample = config->attribute("Sample")->asUint();
            IntegratorData res;
            res.sbtData = packSBT(mGroup.get(), data);
            res.maxSampleDim = 0;
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
        res.guid = Bus::str2GUID("{E8D0D7A4-A55C-443C-A4AB-3EE3E5BA7928}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "PathTracer";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Integrator::getInterface())
            return { "PathTracer" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "PathTracer")
            return std::make_shared<PathTracer>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
