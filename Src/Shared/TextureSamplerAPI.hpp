#pragma once
#include "ConfigAPI.hpp"

struct TextureSamplerData final {
    Data sbtData;
    OptixProgramGroup group;
};

class TextureSampler : public Bus::ModuleFunctionBase {
protected:
    explicit TextureSampler(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.TextureSampler:1";
    }

    virtual TextureSamplerData init(PluginHelper helper,
                                    std::shared_ptr<Config> config) = 0;
};
