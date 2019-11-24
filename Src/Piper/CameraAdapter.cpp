#include "../Shared/ConfigAPI.hpp"
#include "../Shared/PhotographerAPI.hpp"

BUS_MODULE_NAME("Piper.Builtin.CameraAdapter");

// TODO: Fill and Overscan mode
// https://www.scratchapixel.com/lessons/3d-basic-rendering/3d-viewing-pinhole-camera/how-pinhole-camera-works-part-2
class CameraAdapter final : public Photographer {
private:
    std::shared_ptr<Camera> mImpl;
    Vec3 mPos;
    Quat mPosture;
    float mFocalLength, mFStop, mFocalDistance;

public:
    explicit CameraAdapter(Bus::ModuleInstance& instance)
        : Photographer(instance) {}
    CameraData init(PluginHelper helper, std::shared_ptr<Config> config) {
        BUS_TRACE_BEG() {
            reporter().apply(Bus::ReportLevel::Info, "Loading camera",
                             BUS_DEFSRCLOC());
            auto ccfg = config->attribute("Camera");
            auto name = ccfg->attribute("Plugin")->asString();
            mImpl = system().instantiateByName<Camera>(name);
            if(!mImpl)
                BUS_TRACE_THROW(
                    std::runtime_error("Failed to load camera plugin " + name));
            auto res = mImpl->init(helper, ccfg);
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
            return res;
        }
        BUS_TRACE_END();
    }

    Data prepareFrame(Uint2 filmSize) {
        return mImpl->setArgs(mFocalLength, mFStop, mFocalDistance, filmSize,
                              mPos, mPosture);
    }
};

std::shared_ptr<Bus::ModuleFunctionBase>
makeCameraAdapter(Bus::ModuleInstance& instance) {
    return std::make_shared<CameraAdapter>(instance);
}
