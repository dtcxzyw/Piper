#pragma once
#include "../../Shared/Shared.hpp"

struct DataDesc final {
    // All DF_* functions of one material DF use the same target argument block.
    const char* argData;
    CUdeviceptr resource;
};
