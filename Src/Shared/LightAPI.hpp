#pragma once
#include "PluginShared.hpp"

struct LightData final {
    Data sbtData;
    unsigned maxSampleDim;
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
