#include "CameraAdapter.hpp"

BUS_MODULE_NAME("Piper.Builtin.CameraAdapter");

CameraAdapter::CameraAdapter(std::shared_ptr<Config> config,
                             PluginHelper helper, Bus::ModuleSystem& sys,
                             CameraData& data) {
    BUS_TRACE_BEG() {
        sys.getReporter().apply(Bus::ReportLevel::Info, "Loading camera",
                                BUS_DEFSRCLOC());
        auto name = config->attribute("Plugin")->asString();
        mImpl = sys.instantiateByName<Camera>(name);
        if(!mImpl)
            BUS_TRACE_THROW(
                std::runtime_error("Failed to load camera plugin " + name));
        data = mImpl->init(helper, config->attribute("Device"));
        mPos = config->attribute("Position")->asVec3();
        mFocalLength = config->attribute("FocalLength")->asFloat();
        mFStop = config->attribute("FStop")->asFloat();
        auto posture = config->attribute("Posture");
        if(posture->getType() == DataType::Array) {
            Vec4 q = posture->asVec4();
            mPosture = Quat(q.x, q.y, q.z, q.w);
            mFocalDistance = config->attribute("FocalDistance")->asFloat();
        } else {
            Vec3 lookAt = posture->attribute("LookAt")->asVec3();
            Vec3 up = posture->attribute("Up")->asVec3();
            mPosture = glm::quatLookAtRH(lookAt - mPos, up);
            mFocalDistance =
                config->getFloat("FocalDistance", length(lookAt - mPos));
        }
    }
    BUS_TRACE_END();
}

Data CameraAdapter::prepare(Uint2 filmSize) {
    return mImpl->setArgs(mFocalLength, mFStop, mFocalDistance, filmSize, mPos,
                          mPosture);
}
