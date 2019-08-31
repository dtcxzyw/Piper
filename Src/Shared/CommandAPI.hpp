#pragma once
#include "PluginShared.hpp"

class Command : public Bus::ModuleFunctionBase {
protected:
    explicit Command(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    static Bus::Name getInterface() {
        return "Piper.Command";
    }
    virtual int doCommand(int argc, char** argv, Bus::ModuleSystem& sys) = 0;
};
