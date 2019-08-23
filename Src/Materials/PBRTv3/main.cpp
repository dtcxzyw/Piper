#define CORRADE_DYNAMIC_PLUGIN
#include "../MaterialAPI.hpp"
#include <functional>
#include <any>

using MaterialBuilder = std::function<void(PluginHelper helper, JsonHelper cfg,
    const fs::path &modulePath, optix::Material mat,
    std::vector<std::any> &contents)>;

static void plastic(PluginHelper helper, JsonHelper cfg, const fs::path &mp,
    optix::Material mat, std::vector<std::any> &contents) {
    optix::Program hitProgram = helper->compile("closestHit",
        { "BxDF.ptx", "Fresnel.ptx", "Microfact.ptx", "Plastic.ptx" }, mp);
    mat->setClosestHitProgram(radianceRayType, hitProgram);
    contents.emplace_back(hitProgram);
    auto bindTexture = [&] (const std::string &attr, TextureChannel channel) {
        TextureHolder tex = helper->loadTexture(channel, cfg->attribute(attr));
        contents.emplace_back(tex);
        mat["material" + attr]->set(tex.sampler);
    };
    bindTexture("Kd", TextureChannel::Float4);
    bindTexture("Ks", TextureChannel::Float4);
    bindTexture("Roughness", TextureChannel::Float2);
}

static const std::map<std::string, MaterialBuilder> builders = {
    { "Plastic", plastic }
};

class PBRTv3 final : public Material {
private:
    optix::Material mMaterial;
    std::vector<std::any> mContents;
public:
    explicit PBRTv3(PM::AbstractManager &manager, const std::string &plugin)
        : Material{ manager, plugin } {}

    void init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) override {
        auto context = helper->getContext();
        mMaterial = context->createMaterial();
        std::string typeName = config->toString("Type");
        auto iter = builders.find(typeName);
        if (iter == builders.end())
            throw std::runtime_error("No PBRTv3's material named \""
            + typeName + "\"");
        iter->second(helper, config, modulePath, mMaterial, mContents);
        mMaterial->validate();
    }

    optix::Material getMaterial() const override {
        return mMaterial;
    }
};
CORRADE_PLUGIN_REGISTER(PBRTv3, PBRTv3, "Piper.Material:1")
