#include "../../Shared/MaterialAPI.hpp"
#include "../../ThirdParty/Bus/BusImpl.cpp"

class Plastic final : public Material {
private:
    optix::Material mMat;
    TextureHolder mKd, mKs, mRoughness;
    optix::Program mHit;

public:
    explicit Plastic(Bus::ModuleInstance& instance) : Material(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> config) override {
        mHit = helper->compile(
            "closestHit",
            { "BxDF.ptx", "Fresnel.ptx", "Microfact.ptx", "Plastic.ptx" },
            modulePath().parent_path());
        mMat->setClosestHitProgram(radianceRayType, mHit);
        auto bindTexture = [&](const std::string& attr, TextureChannel channel,
                               TextureHolder& tex) {
            tex = helper->loadTexture(channel, config->attribute(attr));
            mMat["material" + attr]->set(tex.sampler);
        };
        bindTexture("Kd", TextureChannel::Float4, mKd);
        bindTexture("Ks", TextureChannel::Float4, mKs);
        bindTexture("Roughness", TextureChannel::Float2, mRoughness);
    }
    optix::Material getMaterial() const override {
        return mMat;
    }
};

class Instance final : public Bus::ModuleInstance {
public:
    Instance(const fs::path& path, Bus::ModuleSystem& sys)
        : Bus::ModuleInstance(path, sys) {}
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = "Piper.BuiltinMaterial.PBRTv3";
        res.guid = Bus::str2GUID("{022757A7-3D5C-470C-A056-67A00D3EADD0}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "PBRTv3";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Material::getInterface())
            return { "Plastic" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "Plastic")
            return std::make_shared<Plastic>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
