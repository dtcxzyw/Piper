#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE Spectrum __direct_callable__tex(Vec2) {
    auto data = getSBTData<Constant>();
    return data->color;
}
