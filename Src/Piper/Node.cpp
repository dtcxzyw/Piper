#include "../Shared/ConfigAPI.hpp"
#include "../Shared/GeometryAPI.hpp"
#include "../Shared/LightAPI.hpp"
#pragma warning(push, 0)
#define NOMINMAX
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinGeometry.Node");

class Node final : public Geometry {
private:
    std::vector<std::shared_ptr<Geometry>> mChildren;
    GeometryData mData;
    Buffer mAccelBuffer, mInstance;

public:
    explicit Node(Bus::ModuleInstance& instance) : Geometry(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> cfg) override {
        BUS_TRACE_BEG() {
            SRT transform = cfg->getTransform("Transform");
            // TODO:pass transform to light
            mData.maxSampleDim = 0;
            auto children = cfg->attribute("Children");
            for(auto&& child : children->expand()) {
                auto type = child->attribute("NodeType")->asString();
                if(type == "Light") {
                    auto light = helper->instantiateAsset<Light>(child);
                    helper->addLight(light);
                } else if(type == "Geometry") {
                    auto geo = helper->instantiateAsset<Geometry>(child);
                    GeometryData data = geo->getData();
                    mData.maxSampleDim =
                        std::max(mData.maxSampleDim, data.maxSampleDim);
                    mChildren.push_back(geo);
                } else {
                    BUS_TRACE_THROW(std::logic_error(
                        "Unrecognized node type \"" + type + "\"."));
                }
            }
            if(mChildren.empty()) {
                mData.handle = 0;
            } else {
                std::vector<OptixInstance> insts;
                for(auto&& child : mChildren) {
                    // TODO:instance description from child
                    OptixInstance inst = {};
                    // TODO:mask
                    inst.visibilityMask = 255;
                    inst.instanceId = 0;
                    // TODO:Disable transform
                    inst.flags = OPTIX_INSTANCE_FLAG_NONE;
                    inst.sbtOffset = 0;
                    *reinterpret_cast<glm::mat3x4*>(inst.transform) =
                        transform.getPointTrans();
                    inst.traversableHandle = child->getData().handle;
                    if(inst.traversableHandle)
                        insts.emplace_back(inst);
                }
                mInstance = uploadData(0, insts.data(), insts.size(),
                                       OPTIX_INSTANCE_BYTE_ALIGNMENT);
                OptixBuildInput input = {};
                input.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
                auto& instInput = input.instanceArray;
                instInput.aabbs = instInput.numAabbs = 0;
                instInput.numInstances = static_cast<unsigned>(insts.size());
                instInput.instances = asPtr(mInstance);

                OptixAccelBuildOptions opt = {};
                opt.operation = OPTIX_BUILD_OPERATION_BUILD;
                opt.motionOptions.numKeys = 1;

                OptixAccelBufferSizes size;

                checkOptixError(optixAccelComputeMemoryUsage(
                    helper->getContext(), &opt, &input, 1, &size));

                Buffer tmp = allocBuffer(size.tempSizeInBytes,
                                         OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT);
                mAccelBuffer = allocBuffer(size.outputSizeInBytes,
                                           OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT);
                checkOptixError(optixAccelBuild(
                    helper->getContext(), 0, &opt, &input, 1, asPtr(tmp),
                    size.tempSizeInBytes, asPtr(mAccelBuffer),
                    size.outputSizeInBytes, &mData.handle, nullptr, 0));
                checkCudaError(cuStreamSynchronize(0));
            }
        }
        BUS_TRACE_END();
    }
    GeometryData getData() override {
        return mData;
    }
};

std::shared_ptr<Bus::ModuleFunctionBase>
makeNode(Bus::ModuleInstance& instance) {
    return std::make_shared<Node>(instance);
}
