#pragma once
#include "../Shared/CameraAPI.hpp"

class CameraAdapter final : Unmoveable {
private:
    std::shared_ptr<Camera> mImpl;
    Vec3 mPos;
    Quaternion mPosture;
    float mFocalLength, mFStop, mFocalDistance;
    fs::path mPTX;

public:
    explicit CameraAdapter(std::shared_ptr<Config> config, PluginHelper helper,
                           Bus::ModuleSystem& sys);
    void prepare(optix::Program rayGen, uint2 filmSize);
    fs::path getPTX() const;
};
