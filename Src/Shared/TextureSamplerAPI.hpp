#pragma once
#include "PluginShared.hpp"

struct TextureSamplerData final {
    Data sbtData;
    OptixProgramGroup group;
};

class TextureSampler : public Asset {
protected:
    explicit TextureSampler(Bus::ModuleInstance& instance) : Asset(instance) {}

public:
    static Name getInterface() {
        return "Piper.TextureSampler:1";
    }

    virtual TextureSamplerData getData() = 0;
};
