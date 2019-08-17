#include "CameraAdapter.hpp"
#pragma warning(push,0)
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

CameraAdapter::CameraAdapter(JsonHelper config, PluginHelper helper)
    :mImpl(config->toString("Plugin")) {
    mPTX = mImpl->init(helper, config->attribute("Device"),
        fs::path("Plugins/Cameras") / mImpl->plugin());
    mPos = config->toVec3("Position");
    mFocalLength = config->toFloat("FocalLength");
    mFStop = config->toFloat("FStop");
    JsonHelper posture = config->attribute("Posture");
    if (posture->getType() == Json::value_t::array)
        mPosture = config->toVec4("Posture");
    else {
        Vec3 lookAt = posture->toVec3("LookAt");
        Vec3 up = posture->toVec3("Up");
        auto cast = [] (const Vec3 &v) {
            return glm::vec3(v.x, v.y, v.z);
        };
        glm::quat q = glm::quatLookAtRH(cast(lookAt - mPos), cast(up));
        mPosture = Quaternion(q.x, q.y, q.z, q.w);
    }
}

void CameraAdapter::prepare(optix::Program rayGen, uint2 filmSize) {
    mImpl->setArgs(rayGen, mFocalLength, mFStop, filmSize, mPos, mPosture);
}

fs::path CameraAdapter::getPTX() const {
    return mPTX;
}
