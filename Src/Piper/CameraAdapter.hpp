#pragma once
#include "../Shared/CameraAPI.hpp"

class CameraAdapter final : Unmoveable {
private:
    std::shared_ptr<Camera> mImpl;
    Vec3 mPos;
    Quat mPosture;
    float mFocalLength, mFStop, mFocalDistance;

public:
    explicit CameraAdapter(std::shared_ptr<Config> config, PluginHelper helper,
                           Bus::ModuleSystem& sys, CameraData& data);
    Data prepare(Uint2 filmSize);
};
