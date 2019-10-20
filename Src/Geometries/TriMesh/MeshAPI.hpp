#pragma once
#include "../../Shared/ConfigAPI.hpp"

struct MeshData final {
    void *vertex, *index, *normal, *texCoord;
    unsigned vertexSize, indexSize;
};

class Mesh : public Asset {
protected:
    explicit Mesh(Bus::ModuleInstance& instance) : Asset(instance) {}

public:
    static Name getInterface() {
        return "Piper.Mesh:1";
    }
    virtual MeshData getData() = 0;
};
