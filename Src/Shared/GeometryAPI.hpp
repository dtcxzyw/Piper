#pragma once
#include "ConfigAPI.hpp"

struct GeometryData final {
    Data sbtData;
    unsigned maxSampleDim;
    OptixTraversableHandle handle;
};

class Geometry : public Bus::ModuleFunctionBase {
protected:
    explicit Geometry(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Geometry:1";
    }

    virtual GeometryData init(PluginHelper helper,
                              std::shared_ptr<Config> config,
                              uint32_t& hitGroupOffset) = 0;
};
