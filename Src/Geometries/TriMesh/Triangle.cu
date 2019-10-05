#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

GLOBAL void __closesthit__RCH() {
    printf("hit\n");
    const DataDesc* data =
        reinterpret_cast<DataDesc*>(optixGetSbtDataPointer());
    float2 vw = optixGetTriangleBarycentrics();
    Payload* payload = getPayload();
    payload->hit = false;
    payload->rad =
        Spectrum{ 1.0f, 1.0f,
                  1.0f };  // Spectrum{ 1.0f - vw.x - vw.y, vw.x, vw.y };
}

GLOBAL void __anyhit__OAH() {}
