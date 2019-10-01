#pragma once
#include "ConfigAPI.hpp"

struct MaterialData final {
    Data radData, occData;
    unsigned maxSampleDim;
    std::vector<OptixProgramGroup> group;
};

class Material : public Bus::ModuleFunctionBase {
protected:
    explicit Material(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Material:1";
    }

    virtual MaterialData init(PluginHelper helper,
                              std::shared_ptr<Config> config) = 0;
};
