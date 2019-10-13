#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE Spectrum __direct_callable__tex(Vec2) {
    return getSBTData<Constant>()->color;
}
