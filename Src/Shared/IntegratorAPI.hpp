#pragma once
#include "ConfigAPI.hpp"

class Integrator : public Bus::ModuleFunctionBase {
protected:
    explicit Integrator(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static std::string getInterface() {
        return "Piper.Integrator:1";
    }

    virtual optix::Program init(PluginHelper helper,
                                std::shared_ptr<Config> config,
                                const fs::path& cameraPTX) = 0;
};
