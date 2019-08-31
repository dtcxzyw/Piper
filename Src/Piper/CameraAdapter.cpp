#include "CameraAdapter.hpp"
#pragma warning(push, 0)
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

CameraAdapter::CameraAdapter(std::shared_ptr<Config> config,
                             PluginHelper helper, Bus::ModuleSystem& sys) {
    mImpl =
        sys.instantiateByName<Camera>(config->attribute("Plugin")->asString());
    mPTX = mImpl->init(helper, config->attribute("Device"));
    mPos = config->attribute("Position")->asVec3();
    mFocalLength = config->attribute("FocalLength")->asFloat();
    mFStop = config->attribute("FStop")->asFloat();
    auto posture = config->attribute("Posture");
    if(posture->getType() == DataType::Array) {
        mPosture = posture->asVec4();
        mFocalDistance = config->attribute("FocalDistance")->asFloat();
    } else {
        Vec3 lookAt = posture->attribute("LookAt")->asVec3();
        Vec3 up = posture->attribute("Up")->asVec3();
        auto cast = [](const Vec3& v) { return glm::vec3(v.x, v.y, v.z); };
        glm::quat q = glm::quatLookAtRH(cast(lookAt - mPos), cast(up));
        mPosture = Quaternion(q.x, q.y, q.z, q.w);
        mFocalDistance =
            config->getFloat("FocalDistance", length(lookAt - mPos));
    }
}

void CameraAdapter::prepare(optix::Program rayGen, uint2 filmSize) {
    mImpl->setArgs(rayGen, mFocalLength, mFStop, mFocalDistance, filmSize, mPos,
                   mPosture);
}

fs::path CameraAdapter::getPTX() const {
    return mPTX;
}
