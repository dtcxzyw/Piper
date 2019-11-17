#pragma once
#include "PluginShared.hpp"

struct IntegratorData final {
    Data sbtData;
    unsigned maxSampleDim, dimFactor;
    OptixProgramGroup group;
};

class Integrator : public Bus::ModuleFunctionBase {
protected:
    explicit Integrator(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static std::string getInterface() {
        return "Piper.Integrator:1";
    }

    virtual IntegratorData init(PluginHelper helper,
                                std::shared_ptr<Config> config) = 0;
};
