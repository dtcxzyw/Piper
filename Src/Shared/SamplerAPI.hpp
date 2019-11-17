#pragma once
#include "PluginShared.hpp"

struct SamplerData final {
    std::vector<Data> sbtData;
    std::vector<OptixProgramGroup> group;
    unsigned maxSPP;
};

class Sampler : public Bus::ModuleFunctionBase {
protected:
    explicit Sampler(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Sampler:1";
    }

    virtual SamplerData init(PluginHelper helper,
                             std::shared_ptr<Config> config, Uint2 size,
                             unsigned maxDim) = 0;
};
