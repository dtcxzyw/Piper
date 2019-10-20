#pragma once
#include "PluginShared.hpp"

struct MaterialData final {
    Data radData;
    unsigned maxSampleDim;
    OptixProgramGroup group;
};

class Material : public Asset {
protected:
    explicit Material(Bus::ModuleInstance& instance) : Asset(instance) {}

public:
    static Name getInterface() {
        return "Piper.Material:1";
    }

    virtual MaterialData getData() = 0;
};
