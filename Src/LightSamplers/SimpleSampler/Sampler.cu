#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE LightSample __continuation_callable__sample(const Vec3& pos,
                                                   float rayTime,
                                                   uint32_t& seed) {
    unsigned num = getSBTData<DataDesc>()->lightNum;
    if(num == 0) {
        LightSample res;
        res.rad = Spectrum{ 0.0f };
        return res;
    }
    unsigned id = static_cast<unsigned>(num * sample<0>(++seed));
    id = glm::clamp(id, 0U, num - 1U);
    return optixContinuationCall<LightSample, const Vec3&, float, uint32_t&>(
        launchParam.lightSbtOffset + id, pos, rayTime, seed);
}
