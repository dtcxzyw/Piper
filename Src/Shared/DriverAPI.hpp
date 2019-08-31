#pragma once
#include "ConfigAPI.hpp"

class Driver : public Bus::ModuleFunctionBase {
protected:
    explicit Driver(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Driver:1";
    }

    virtual uint2 init(PluginHelper helper, std::shared_ptr<Config> config) = 0;
    virtual void doRender() = 0;
};
