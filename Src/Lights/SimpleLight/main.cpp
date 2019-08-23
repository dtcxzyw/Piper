#define CORRADE_DYNAMIC_PLUGIN
#include "../LightAPI.hpp"
#include <functional>
#include <any>
#include "DataDesc.hpp"

using LightBuilder = std::function<LightProgram(PluginHelper helper,
    JsonHelper cfg, const fs::path &modulePath, optix::Program &prog,
    std::vector<std::any> &contents)>;

static LightProgram dirLight(PluginHelper helper, JsonHelper cfg,
    const fs::path &mp, optix::Program &prog,
    std::vector<std::any> &contents) {
    optix::Context context = helper->getContext();
    optix::Buffer buf = context->createBuffer(RT_BUFFER_INPUT,
        RTformat::RT_FORMAT_BYTE, sizeof(DirLight));
    {
        BufferMapGuard guard(buf, RT_BUFFER_MAP_WRITE_DISCARD);
        DirLight &data = guard.ref<DirLight>();
        data.lum = cfg->toVec3("Lum");
        data.direction = normalize(cfg->toVec3("Direction"));
        data.distance = cfg->toFloat("Distance");
    }
    buf->validate();
    contents.emplace_back(buf);
    prog = helper->compile("sample", { "DirLight.ptx" }, mp);
    LightProgram res;
    res.prog = prog->getId();
    res.buf = buf->getId();
    return res;
}

static const std::map<std::string, LightBuilder> builders = {
    { "DirectionalLight", dirLight }
};

class SimpleLight final : public Light {
private:
    std::vector<std::any> mContents;
    optix::Program mProgram;
public:
    explicit SimpleLight(PM::AbstractManager &manager,
        const std::string &plugin) : Light{ manager, plugin } {}
    LightProgram init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) override {
        auto context = helper->getContext();
        std::string typeName = config->toString("Type");
        auto iter = builders.find(typeName);
        if (iter == builders.end())
            throw std::runtime_error("No SimpleLight's light named \""
            + typeName + "\"");
        return iter->second(helper, config, modulePath, mProgram, mContents);
    }
    optix::Program getProgram() override {
        return mProgram;
    }
};
CORRADE_PLUGIN_REGISTER(SimpleLight, SimpleLight, "Piper.Light:1")
