#pragma once
#include "ConfigAPI.hpp"

class Material : public Bus::ModuleFunctionBase {
protected:
    explicit Material(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Material:1";
    }

    virtual void init(PluginHelper helper, std::shared_ptr<Config> config) = 0;

    virtual optix::Material getMaterial() const = 0;
};
