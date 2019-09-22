#include "../Shared/CommandAPI.hpp"
#include "../Shared/ConfigAPI.hpp"
#include "../Shared/DriverAPI.hpp"
#include "../Shared/GeometryAPI.hpp"
#include "../Shared/IntegratorAPI.hpp"
#include "../Shared/LightAPI.hpp"
#include "../Shared/MaterialAPI.hpp"
#include "CameraAdapter.hpp"
#include <any>

std::unique_ptr<PluginHelperAPI>
buildPluginHelper(OptixDeviceContext context, const fs::path& scenePath,
                  bool debug, const OptixModuleCompileOptions& MCO,
                  const OptixPipelineCompileOptions& PCO);

BUS_MODULE_NAME("Piper.Builtin.Renderer");

void logCallBack(unsigned int lev, const char* tag, const char* message,
                 void* cbdata) {
    Bus::Reporter* reporter = reinterpret_cast<Bus::Reporter*>(cbdata);
    ReportLevel level = ReportLevel::Info;
    std::string pre;
    switch(lev) {
        case 1:
            pre = "[FATAL]", level = ReportLevel::Error;
            break;
        case 2:
            level = ReportLevel::Error;
            break;
        case 3:
            level = ReportLevel::Warning;
            break;
        default:
            pre = "[UNKNOWN]";
            break;
    }
    reporter->apply(level, pre + "[" + tag + "]" + message,
                    BUS_SRCLOC("OptixEngine"));
}

struct CUDAContextDeleter final {
    void operator()(CUcontext*) const {
        CUdevice dev;
        checkCudaError(cuCtxGetDevice(&dev));
        checkCudaError(cuDevicePrimaryCtxRelease(dev));
    }
};
using CUDAContext = std::unique_ptr<CUctx_st, CUDAContextDeleter>;

CUDAContext createCUDAContext(Bus::Reporter& reporter) {
    BUS_TRACE_BEG() {
        checkCudaError(cuInit(0));
        int drver;
        checkCudaError(cuDriverGetVersion(&drver));
        reporter.apply(ReportLevel::Info,
                       "driver's CUDA version:" + std::to_string(drver / 1000) +
                           "." + std::to_string(drver % 1000 / 10),
                       BUS_DEFSRCLOC());
        CUdevice dev;
        checkCudaError(cuDeviceGet(device, 0));
        char buf[256];
        checkCudaError(cuDeviceGetName(buf, sizeof(buf), dev));
        reporter.apply(ReportLevel::Info, "use device " + buf, BUS_DEFSRCLOC());
        CUcontext ctx;
        checkCudaError(cuDevicePrimaryCtxRetain(&ctx, dev));
        return CUDAContext{ ctx };
    }
    BUS_TRACE_END();
}

struct ContextDeleter final {
    void operator()(OptixDeviceContext ptr) const {
        checkOptixError(optixDeviceContextDestroy(ptr));
    }
};
using Context = std::unique_ptr<OptixDeviceContext_t, ContextDeleter>;

Context createContext(CUcontext ctx, Bus::Reporter& reporter,
                      std::shared_ptr<Config> config) {
    BUS_TRACE_BEG() {
        checkOptixError(optixInit());
        OptixDeviceContextOptions opt;
        opt.logCallbackLevel =
            static_cast<int>(config->attribute("LogLevel")->asUint());
        opt.logCallbackFunction = logCallBack;
        opt.logCallbackData = &reporter;
        OptixDeviceContext context;
        checkOptixError(optixDeviceContextCreate(ctx, &opt, &context));
        checkOptixError(optixDeviceContextSetCacheEnabled(context, 1));
        checkOptixError(
            optixDeviceContextSetCacheLocation(context, "Cache/Optix"));
        checkOptixError(optixDeviceContextSetCacheDatabaseSizes(
            context, 128 << 20, 512 << 20));
        unsigned rtver = 0;
        checkOptixError(optixDeviceContextGetProperty(
            context, OptixDeviceProperty::OPTIX_DEVICE_PROPERTY_RTCORE_VERSION,
            &rtver, sizeof(rtver)));
        reporter.apply(ReportLevel::Info,
                       "RTCore Version:" + std::to_string(rtver),
                       BUS_DEFSRCLOC());
        return Context{ context };
    }
    BUS_TRACE_END();
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
    BUS_TRACE_BEG() {
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
    BUS_TRACE_BEG() {
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
    BUS_TRACE_BEG() {
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
    BUS_TRACE_BEG() {
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
    BUS_TRACE_BEG() {
        CUDAContext ctx = createCUDAContext(sys.getReporter());
        auto global = config->attribute("Core");
        Context context = createContext(ctx.get(), sys.getReporter(), global);
        bool debug = global->attribute("Debug")->asBool();
        OptixModuleCompileOptions MCO;
        MCO.debugLevel = (debug ? OPTIX_COMPILE_DEBUG_LEVEL_FULL :
                                  OPTIX_COMPILE_DEBUG_LEVEL_NONE);
        MCO.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
        MCO.optLevel = (debug ? OPTIX_COMPILE_OPTIMIZATION_LEVEL_0 :
                                OPTIX_COMPILE_OPTIMIZATION_LEVEL_3);
        OptixPipelineCompileOptions PCO;
        PCO.exceptionFlags = OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW |
            OPTIX_EXCEPTION_FLAG_TRACE_DEPTH;
        if(debug)
            PCO.exceptionFlags |= OPTIX_EXCEPTION_FLAG_DEBUG;
        PCO.pipelineLaunchParamsVariableName = "globalLaunchParam";
        PCO.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;
        PCO.usesMotionBlur = false;
        PCO.numAttributeValues = PCO.numPayloadValues = 3;
        std::shared_ptr<PluginHelperAPI> helper =
            buildPluginHelper(context.get(), scenePath, debug, MCO, PCO);
        auto& reporter = sys.getReporter();
        reporter.apply(Bus::ReportLevel::Info, "Loading camera",
                       BUS_DEFSRCLOC());
        CameraAdapter camera(config->attribute("Camera"), helper, sys);
        reporter.apply(Bus::ReportLevel::Info, "Loading materials",
                       BUS_DEFSRCLOC());
        MaterialLib materials =
            loadMaterials(config->attribute("MaterialLib"), helper, sys);
        reporter.apply(Bus::ReportLevel::Info, "Loading lights",
                       BUS_DEFSRCLOC());
        std::vector<std::shared_ptr<Light>> lig;
        LightLib lights =
            loadLights(config->attribute("LightLib"), helper, lig, sys);
        reporter.apply(Bus::ReportLevel::Info, "Configurating lights",
                       BUS_DEFSRCLOC());
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
                       BUS_DEFSRCLOC());
        optix::Program inteProg;
        std::shared_ptr<Integrator> integrator =
            loadIntegrator(config->attribute("Integrator"), helper,
                           camera.getPTX(), inteProg, sys);
        context->setRayGenerationProgram(0, inteProg);
        reporter.apply(Bus::ReportLevel::Info, "Loading driver",
                       BUS_DEFSRCLOC());
        uint2 filmSize;
        std::shared_ptr<Driver> driver =
            loadDriver(config->attribute("Driver"), helper, filmSize, sys);
        camera.prepare(inteProg, filmSize);
        reporter.apply(Bus::ReportLevel::Info, "Loading geometries",
                       BUS_DEFSRCLOC());
        GeometryLib geos =
            loadGeometries(config->attribute("GeometryLib"), helper, sys);
        reporter.apply(Bus::ReportLevel::Info, "Loading scene",
                       BUS_DEFSRCLOC());
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
                       BUS_DEFSRCLOC());
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
                                BUS_DEFSRCLOC());
        return EXIT_FAILURE;
    }
};

std::shared_ptr<Bus::ModuleFunctionBase>
makeRenderer(Bus::ModuleInstance& instance) {
    return std::make_shared<Renderer>(instance);
}
