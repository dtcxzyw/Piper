#pragma once
#include "PluginShared.hpp"

struct GeometryData final {
    unsigned maxSampleDim;
    OptixTraversableHandle handle;
};

class Geometry : public Asset {
protected:
    explicit Geometry(Bus::ModuleInstance& instance) : Asset(instance) {}

public:
    static Name getInterface() {
        return "Piper.Geometry:1";
    }

    virtual GeometryData getData() = 0;
};
