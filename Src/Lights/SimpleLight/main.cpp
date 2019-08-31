#include "../../Shared/LightAPI.hpp"
#include "DataDesc.hpp"

class DirectionalLight final : public Light {
private:
    optix::Program mProgram;
    optix::Buffer mBuf;

public:
    explicit DirectionalLight(Bus::ModuleInstance& instance)
        : Light(instance) {}
    LightProgram init(PluginHelper helper,
                      std::shared_ptr<Config> cfg) override {
        optix::Context context = helper->getContext();
        mBuf = context->createBuffer(RT_BUFFER_INPUT, RTformat::RT_FORMAT_BYTE,
                                     sizeof(DirLight));
        {
            BufferMapGuard guard(mBuf, RT_BUFFER_MAP_WRITE_DISCARD);
            DirLight& data = guard.ref<DirLight>();
            data.lum = cfg->attribute("Lum")->asVec3();
            data.direction = normalize(cfg->attribute("Direction")->asVec3());
            data.distance = cfg->attribute("Distance")->asFloat();
        }
        mBuf->validate();
        mProgram = helper->compile("sample", { "DirLight.ptx" },
                                   modulePath().parent_path());
        LightProgram res;
        res.prog = mProgram->getId();
        res.buf = mBuf->getId();
        return res;
    }
    optix::Program getProgram() override {
        return mProgram;
    }
};

class Instance final : public Bus::ModuleInstance {
public:
    Instance(const fs::path& path, Bus::ModuleSystem& sys)
        : Bus::ModuleInstance(path, sys) {}
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = "Piper.BuiltinLight.SimpleLight";
        res.guid = Bus::str2GUID("{5B55F22A-2BDB-43B6-8EED-0CE0FBB49949}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "SimpleLight";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Light::getInterface())
            return { "DirectionalLight" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "DirectionalLight")
            return std::make_shared<DirectionalLight>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
