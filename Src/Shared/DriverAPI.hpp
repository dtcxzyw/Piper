#pragma once
#include "PluginShared.hpp"
#include <functional>

class DriverHelperAPI : private Unmoveable {
public:
    virtual void
    doRender(const std::function<void(OptixShaderBindingTable&)>& callBack) = 0;
    virtual CUstream getStream() const = 0;
};
using DriverHelper = DriverHelperAPI*;

struct DriverData final {
    std::vector<OptixProgramGroup> group;
    Uint2 size;
};

class Driver : public Bus::ModuleFunctionBase {
protected:
    explicit Driver(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Name getInterface() {
        return "Piper.Driver:1";
    }

    virtual DriverData init(PluginHelper helper,
                            std::shared_ptr<Config> config) = 0;
    virtual void doRender(DriverHelper helper) = 0;
};
