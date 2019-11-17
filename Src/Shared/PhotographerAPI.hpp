#pragma once
#include "CameraAPI.hpp"

class Photographer : public Bus::ModuleFunctionBase {
protected:
    explicit Photographer(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Photographer:1";
    }

    virtual CameraData init(PluginHelper helper,
                            std::shared_ptr<Config> config) = 0;

    virtual Data prepareFrame(Uint2 filmSize) = 0;
};
