#include "../Shared/ConfigAPI.hpp"
#include "../Shared/GeometryAPI.hpp"
#include "../Shared/LightAPI.hpp"

BUS_MODULE_NAME("Piper.BuiltinGeometry.Node");

class Node final : public Geometry {
private:
    std::vector<std::shared_ptr<Geometry>> mChildren;
    GeometryData mData;

public:
    explicit Node(Bus::ModuleInstance& instance) : Geometry(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> cfg) override {
        BUS_TRACE_BEG() {
            // TODO:transform
            // SRT transform = cfg->getTransform("Transform");
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
            } else if(mChildren.size() == 1) {
                mData.handle = mChildren.front()->getData().handle;
            } else {
                // TODO:instance array
                BUS_TRACE_THROW(std::logic_error("Unimplemented feature."));
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
