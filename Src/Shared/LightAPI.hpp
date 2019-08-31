#pragma once
#include "ConfigAPI.hpp"

struct LightProgram final {
    int prog, buf;
};

class Light : public Bus::ModuleFunctionBase {
protected:
    explicit Light(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Light:1";
    }

    virtual LightProgram init(PluginHelper helper,
                              std::shared_ptr<Config> config) = 0;
    virtual optix::Program getProgram() = 0;
};
