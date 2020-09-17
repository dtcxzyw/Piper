#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE void __miss__rad() {
    getPayload()->rad = getSBTData<ConstantData>()->lum;
}
