#include "../../Shared/IntegratorAPI.hpp"

class PathTracer final : public Integrator {
private:
    optix::Program mProgram;

public:
    explicit PathTracer(Bus::ModuleInstance& instance) : Integrator(instance) {}
    optix::Program init(PluginHelper helper, std::shared_ptr<Config> config,
                        const fs::path& cameraPTX) override {
        mProgram = helper->compile("traceKernel", { "PathKernel.ptx" },
                                   modulePath().parent_path(), { cameraPTX });
        mProgram["integratorSample"]->setUint(
            config->attribute("Sample")->asUint());
        mProgram["integratorMaxDepth"]->setUint(
            config->attribute("MaxDepth")->asUint());
        return mProgram;
    }
};

class Instance final : public Bus::ModuleInstance {
public:
    Instance(const fs::path& path, Bus::ModuleSystem& sys)
        : Bus::ModuleInstance(path, sys) {}
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = "Piper.BuiltinIntegrator.PathTracer";
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
