#pragma once
#include "ConfigAPI.hpp"

struct LightSamplerData final {
    Data sbtData;
    unsigned maxSampleDim;
    OptixProgramGroup group;
};

class LightSampler : public Bus::ModuleFunctionBase {
protected:
    explicit LightSampler(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.LightSampler:1";
    }

    virtual LightSamplerData init(PluginHelper helper,
                                  std::shared_ptr<Config> config,
                                  size_t lightNum) = 0;
};
