#include "../../Shared/KernelShared.hpp"
#include "DataDesc.hpp"

DEVICE LightSample __continuation_callable__sample(const Vec3& pos,
                                                   float rayTime,
                                                   SamplerContext& sampler) {
    auto data = getSBTData<DataDesc>();
    unsigned num = data->lightNum;
    if(num == 0) {
        LightSample res;
        res.rad = Spectrum{ 0.0f };
        return res;
    }
    unsigned id = static_cast<unsigned>(num * sampler());
    id = glm::clamp(id, 0U, num - 1U);
    LightSample res = sampleOneLightImpl(launchParam.lightSbtOffset + id, pos,
                                         rayTime, sampler);
    res.rad *= data->invPdf;
    return res;
}

void check(LightSampleFunction = __continuation_callable__sample);
