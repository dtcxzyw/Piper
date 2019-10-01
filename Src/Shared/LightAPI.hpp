#pragma once
#include "ConfigAPI.hpp"

struct LightData final {
    Data sbtData;
    unsigned maxSampleDim;
    OptixProgramGroup group;
};

class Light : public Bus::ModuleFunctionBase {
protected:
    explicit Light(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Light:1";
    }

    virtual LightData init(PluginHelper helper,
                           std::shared_ptr<Config> config) = 0;
};
