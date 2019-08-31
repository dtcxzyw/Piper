#pragma once
#include "ConfigAPI.hpp"

class Geometry : public Bus::ModuleFunctionBase {
protected:
    explicit Geometry(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Geometry:1";
    }

    virtual void init(PluginHelper helper, std::shared_ptr<Config> config) = 0;
    virtual void setInstance(optix::GeometryInstance inst) = 0;
};
