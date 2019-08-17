#include "ModuleLoader.hpp"
#include "../Cameras/CameraAPI.hpp"

class CameraAdapter final :Unmoveable {
private:
    Plugin<Camera> mImpl;
    Vec3 mPos;
    Quaternion mPosture;
    float mFocalLength, mFStop;
    fs::path mPTX;
public:
    explicit CameraAdapter(JsonHelper config, PluginHelper helper);
    void prepare(optix::Program rayGen, uint2 filmSize);
    fs::path getPTX() const;
};
