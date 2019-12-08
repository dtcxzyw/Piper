#pragma once
#include "PluginShared.hpp"

struct LightData final {
    Data sbtData;
    unsigned maxSampleDim, dss, css;
    OptixProgramGroup group;
};

class Light : public Asset {
protected:
    explicit Light(Bus::ModuleInstance& instance) : Asset(instance) {}

public:
    static Name getInterface() {
        return "Piper.Light:1";
    }

    virtual LightData getData() = 0;
};

class EnvironmentLight : public Bus::ModuleFunctionBase {
protected:
    explicit EnvironmentLight(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.EnvironmentLight:1";
    }
    virtual LightData init(PluginHelper helper,
                           std::shared_ptr<Config> config) = 0;
};
