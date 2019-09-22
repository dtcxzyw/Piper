#pragma once
#include "ConfigAPI.hpp"

class Sampler : public Bus::ModuleFunctionBase {
protected:
    explicit Sampler(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Sampler:1";
    }

    virtual std::vector<Data> init(PluginHelper helper,
                                   std::shared_ptr<Config> config,
                                   unsigned maxDim) = 0;
};
