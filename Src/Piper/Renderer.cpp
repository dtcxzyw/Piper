#include "../Shared/CommandAPI.hpp"
#include "../Shared/ConfigAPI.hpp"
#include "../Shared/DriverAPI.hpp"
#include "../Shared/GeometryAPI.hpp"
#include "../Shared/IntegratorAPI.hpp"
#include "../Shared/LightAPI.hpp"
#include "../Shared/MaterialAPI.hpp"
#include "CameraAdapter.hpp"
#include <any>

PluginHelper buildPluginHelper(optix::Context context,
                               const fs::path& runtimeLib,
                               const fs::path& scenePath);

optix::Context createContext(std::shared_ptr<Config> config) {
    int RTXMode = config->getBool("RTXMode", false);
    RTresult res = rtGlobalSetAttribute(RT_GLOBAL_ATTRIBUTE_ENABLE_RTX,
                                        sizeof(int), &RTXMode);
    optix::Context context = optix::Context::create();
    context->checkErrorNoGetContext(res);
    context->setExceptionEnabled(RT_EXCEPTION_ALL, true);
    context->setEntryPointCount(1);
    context->setRayTypeCount(2);
    context->setDiskCacheLocation("Cache/Optix");
    context->setAttribute(RT_CONTEXT_ATTRIBUTE_DISK_CACHE_ENABLED, 1);
    context->setPrintEnabled(true);
    context->setPrintBufferSize(1048576);
    return context;
}

std::shared_ptr<Config> loadScene(Bus::ModuleSystem& sys,
                                  const fs::path& path) {
    for(auto id : sys.list<Config>()) {
        std::shared_ptr<Config> config = sys.instantiate<Config>(id);
        if(config->load(path))
            return config;
    }
    return nullptr;
}

using MaterialLib = std::map<std::string, std::shared_ptr<Material>>;

MaterialLib loadMaterials(std::shared_ptr<Config> config, PluginHelper helper,
                          Bus::ModuleSystem& sys) {
    BUS_TRACE_BEGIN("Piper.Builtin.Renderer") {
        MaterialLib res;
        for(auto cfg : config->expand()) {
            std::string name = cfg->attribute("Name")->asString();
            if(res.count(name)) {
                BUS_TRACE_THROW(std::logic_error("Material \"" + name +
                                                 "\" has been defined."));
            } else {
                auto mat = sys.instantiateByName<Material>(
                    cfg->attribute("Plugin")->asString());
                mat->init(helper, cfg);
                res.emplace(name, mat);
            }
        }
        return res;
    }
    BUS_TRACE_END();
}

using LightLib = std::map<std::string, LightProgram>;

LightLib loadLights(std::shared_ptr<Config> config, PluginHelper helper,
                    std::vector<std::shared_ptr<Light>>& lig,
                    Bus::ModuleSystem& sys) {
    BUS_TRACE_BEGIN("Piper.Builtin.Renderer") {
        LightLib res;
        for(auto cfg : config->expand()) {
            std::string name = cfg->attribute("Name")->asString();
            if(res.count(name)) {
                BUS_TRACE_THROW(std::logic_error("Light \"" + name +
                                                 "\" has been defined."));
            } else {
                auto light = sys.instantiateByName<Light>(
                    cfg->attribute("Plugin")->asString());
                res.emplace(name, light->init(helper, cfg));
                lig.emplace_back(light);
            }
        }
        return res;
    }
    BUS_TRACE_END();
}

std::shared_ptr<Integrator> loadIntegrator(std::shared_ptr<Config> config,
                                           PluginHelper helper,
                                           const fs::path& ptx,
                                           optix::Program& prog,
                                           Bus::ModuleSystem& sys) {
    auto inte = sys.instantiateByName<Integrator>(
        config->attribute("Plugin")->asString());
    prog = inte->init(helper, config, ptx);
    return inte;
}

std::shared_ptr<Driver> loadDriver(std::shared_ptr<Config> config,
                                   PluginHelper helper, uint2& filmSize,
                                   Bus::ModuleSystem& sys) {
    auto driver =
        sys.instantiateByName<Driver>(config->attribute("Plugin")->asString());
    filmSize = driver->init(helper, config);
    return driver;
}

using GeometryLib = std::map<std::string, std::shared_ptr<Geometry>>;

GeometryLib loadGeometries(std::shared_ptr<Config> config, PluginHelper helper,
                           Bus::ModuleSystem& sys) {
    BUS_TRACE_BEGIN("Piper.Builtin.Renderer") {
        GeometryLib res;
        for(auto cfg : config->expand()) {
            std::string name = cfg->attribute("Name")->asString();
            if(res.count(name)) {
                BUS_TRACE_THROW(std::logic_error("Geometry \"" + name +
                                                 "\" has been defined."));
            } else {
                auto geo = sys.instantiateByName<Geometry>(
                    cfg->attribute("Plugin")->asString());
                geo->init(helper, cfg);
                res.emplace(name, geo);
            }
        }
        return res;
    }
    BUS_TRACE_END();
}

struct LightInfo final {
    Mat4 trans;
    LightProgram prog;
};

void parseNode(std::shared_ptr<Config> root, const MaterialLib& mat,
               const LightLib& light, const GeometryLib& geo,
               std::vector<LightInfo>& lightInst,
               std::vector<std::any>& content, optix::Group group,
               optix::Context context);

void parseChildren(std::shared_ptr<Config> root, const MaterialLib& mat,
                   const LightLib& light, const GeometryLib& geo,
                   std::vector<LightInfo>& lightInst,
                   std::vector<std::any>& content, optix::Group group,
                   optix::Context context) {
    for(auto child : root->expand())
        parseNode(child, mat, light, geo, lightInst, content, group, context);
}

template <typename T>
T access(const std::map<std::string, T>& lib, const std::string& name) {
    auto iter = lib.find(name);
    if(iter == lib.cend())
        throw std::runtime_error(std::string("No ") + typeid(T).name() +
                                 " called \"" + name + "\".");
    return iter->second;
}

void parseNode(std::shared_ptr<Config> root, const MaterialLib& mat,
               const LightLib& light, const GeometryLib& geo,
               std::vector<LightInfo>& lightInst,
               std::vector<std::any>& content, optix::Group group,
               optix::Context context) {
    BUS_TRACE_BEGIN("Piper.Builtin.Renderer") {
        std::string type = root->attribute("Type")->asString();
        std::string name = root->attribute("Name")->asString();
        if(type == "Light") {
            LightInfo info;
            info.trans = Mat4::identity();
            info.prog = access(light, name);
            lightInst.emplace_back(info);
        } else if(type == "Geometry") {
            optix::GeometryInstance inst = context->createGeometryInstance();
            access(geo, name)->setInstance(inst);
            inst->setMaterialCount(1);
            inst->setMaterial(
                0,
                access(mat, root->attribute("Material")->asString())
                    ->getMaterial());
            inst->validate();
            content.emplace_back(inst);
            optix::GeometryGroup gg = context->createGeometryGroup();
            optix::Acceleration acc = context->createAcceleration("Trbvh");
            gg->setAcceleration(acc);
            gg->addChild(inst);
            gg->setVisibilityMask(255);
            gg->validate();
            context["globalTopNode"]->set(gg);
            content.emplace_back(gg);
            content.emplace_back(acc);
        } else
            BUS_TRACE_THROW(
                std::logic_error("Unrecognized type \"" + type + "\"."));
    }
    BUS_TRACE_END();
}

void renderImpl(std::shared_ptr<Config> config, const fs::path& scenePath,
                Bus::ModuleSystem& sys) {
    BUS_TRACE_BEGIN("Piper.Builtin.Renderer") {
        auto global = config->attribute("Global");
        optix::Context context = createContext(global);
        std::string runtimeLib = global->attribute("RuntimeLib")->asString();
        PluginHelper helper = buildPluginHelper(
            context, fs::current_path() / "Libs" / runtimeLib, scenePath);
        auto& reporter = sys.getReporter();
        reporter.apply(Bus::ReportLevel::Info, "Loading camera",
                       BUS_SRCLOC("Piper.Builtin.Renderer"));
        CameraAdapter camera(config->attribute("Camera"), helper, sys);
        reporter.apply(Bus::ReportLevel::Info, "Loading materials",
                       BUS_SRCLOC("Piper.Builtin.Renderer"));
        MaterialLib materials =
            loadMaterials(config->attribute("MaterialLib"), helper, sys);
        reporter.apply(Bus::ReportLevel::Info, "Loading lights",
                       BUS_SRCLOC("Piper.Builtin.Renderer"));
        std::vector<std::shared_ptr<Light>> lig;
        LightLib lights =
            loadLights(config->attribute("LightLib"), helper, lig, sys);
        reporter.apply(Bus::ReportLevel::Info, "Configurating lights",
                       BUS_SRCLOC("Piper.Builtin.Renderer"));
        std::vector<int> progs;
        for(auto light : lights)
            progs.push_back(light.second.prog);
        for(auto mat : materials) {
            optix::Program prog =
                mat.second->getMaterial()->getClosestHitProgram(
                    radianceRayType);
            prog->setCallsitePotentialCallees("call_in_sampleOneLight", progs);
        }
        reporter.apply(Bus::ReportLevel::Info, "Loading integrator",
                       BUS_SRCLOC("Piper.Builtin.Renderer"));
        optix::Program inteProg;
        std::shared_ptr<Integrator> integrator =
            loadIntegrator(config->attribute("Integrator"), helper,
                           camera.getPTX(), inteProg, sys);
        context->setRayGenerationProgram(0, inteProg);
        reporter.apply(Bus::ReportLevel::Info, "Loading driver",
                       BUS_SRCLOC("Piper.Builtin.Renderer"));
        uint2 filmSize;
        std::shared_ptr<Driver> driver =
            loadDriver(config->attribute("Driver"), helper, filmSize, sys);
        camera.prepare(inteProg, filmSize);
        reporter.apply(Bus::ReportLevel::Info, "Loading geometries",
                       BUS_SRCLOC("Piper.Builtin.Renderer"));
        GeometryLib geos =
            loadGeometries(config->attribute("GeometryLib"), helper, sys);
        reporter.apply(Bus::ReportLevel::Info, "Loading scene",
                       BUS_SRCLOC("Piper.Builtin.Renderer"));
        std::vector<LightInfo> lightInst;
        std::vector<std::any> content;
        optix::Group group = context->createGroup();
        optix::Acceleration acc = context->createAcceleration("Trbvh");
        group->setAcceleration(acc);
        group->setVisibilityMask(255);
        group->validate();
        context["globalTopNode"]->set(group);
        parseChildren(config->attribute("Scene"), materials, lights, geos,
                      lightInst, content, group, context);
        optix::Buffer lightMat = context->createBuffer(
            RT_BUFFER_INPUT, RT_FORMAT_USER, lightInst.size());
        {
            lightMat->setElementSize(sizeof(Mat4));
            BufferMapGuard guard(lightMat, RT_BUFFER_MAP_WRITE_DISCARD);
            for(size_t i = 0; i < lightInst.size(); ++i)
                guard.as<Mat4>()[i] = lightInst[i].trans;
        }
        context["lightMatrices"]->set(lightMat);
        optix::Buffer lightProg = context->createBuffer(
            RT_BUFFER_INPUT, RT_FORMAT_USER, lightInst.size());
        {
            lightProg->setElementSize(sizeof(LightProgram));
            BufferMapGuard guard(lightProg, RT_BUFFER_MAP_WRITE_DISCARD);
            for(size_t i = 0; i < lightInst.size(); ++i)
                guard.as<LightProgram>()[i] = lightInst[i].prog;
        }
        context["lightPrograms"]->set(lightProg);
        reporter.apply(Bus::ReportLevel::Info, "Everything is ready.",
                       BUS_SRCLOC("Piper.Builtin.Renderer"));
        driver->doRender();
    }
    BUS_TRACE_END();
}

class Renderer final : public Command {
public:
    explicit Renderer(Bus::ModuleInstance& instance) : Command(instance) {}
    int doCommand(int argc, char** argv, Bus::ModuleSystem& sys) override {
        if(argc == 2) {
            fs::path in = argv[1];
            auto scene = loadScene(sys, in);
            renderImpl(scene, in.parent_path(), sys);
            return EXIT_SUCCESS;
        }
        sys.getReporter().apply(ReportLevel::Error, "Need two arguments.",
                                BUS_SRCLOC("Piper.Builtin.Renderer"));
        return EXIT_FAILURE;
    }
};

std::shared_ptr<Bus::ModuleFunctionBase>
makeRenderer(Bus::ModuleInstance& instance) {
    return std::make_shared<Renderer>(instance);
}
