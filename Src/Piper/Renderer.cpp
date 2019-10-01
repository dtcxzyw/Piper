#include "../Shared/CommandAPI.hpp"
#include "../Shared/ConfigAPI.hpp"
#include "../Shared/DriverAPI.hpp"
#include "../Shared/GeometryAPI.hpp"
#include "../Shared/IntegratorAPI.hpp"
#include "../Shared/LightAPI.hpp"
#include "../Shared/MaterialAPI.hpp"
#include "../Shared/SamplerAPI.hpp"
#include "CameraAdapter.hpp"
#pragma warning(push, 0)
#define NOMINMAX
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

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
    void operator()(CUcontext) const {
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
        checkCudaError(cuDeviceGet(&dev, 0));
        char buf[256];
        checkCudaError(cuDeviceGetName(buf, sizeof(buf), dev));
        reporter.apply(ReportLevel::Info, std::string("use device ") + buf,
                       BUS_DEFSRCLOC());
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
                       "RTCore Version:" + std::to_string(rtver / 10) + '.' +
                           std::to_string(rtver % 10),
                       BUS_DEFSRCLOC());
        return Context{ context };
    }
    BUS_TRACE_END();
}

std::shared_ptr<Config> loadScene(Bus::ModuleSystem& sys,
                                  const fs::path& path) {
    sys.getReporter().apply(Bus::ReportLevel::Info, "Loading scene",
                            BUS_DEFSRCLOC());
    for(auto id : sys.list<Config>()) {
        std::shared_ptr<Config> config = sys.instantiate<Config>(id);
        if(config->load(path))
            return config;
    }
    return nullptr;
}

std::shared_ptr<Integrator> loadIntegrator(std::shared_ptr<Config> config,
                                           PluginHelper helper,
                                           Bus::ModuleSystem& sys,
                                           IntegratorData& data) {
    sys.getReporter().apply(Bus::ReportLevel::Info, "Loading integrator",
                            BUS_DEFSRCLOC());
    auto inte = sys.instantiateByName<Integrator>(
        config->attribute("Plugin")->asString());
    data = inte->init(helper, config);
    return inte;
}

std::shared_ptr<Driver> loadDriver(std::shared_ptr<Config> config,
                                   PluginHelper helper, Bus::ModuleSystem& sys,
                                   DriverData& data) {
    sys.getReporter().apply(Bus::ReportLevel::Info, "Loading driver",
                            BUS_DEFSRCLOC());
    auto driver =
        sys.instantiateByName<Driver>(config->attribute("Plugin")->asString());
    data = driver->init(helper, config);
    return driver;
}

using GeometryLib = std::map<std::string, std::shared_ptr<Geometry>>;

GeometryLib loadGeometries(std::shared_ptr<Config> config, PluginHelper helper,
                           Bus::ModuleSystem& sys,
                           std::vector<GeometryData>& data) {
    sys.getReporter().apply(Bus::ReportLevel::Info, "Loading geometries",
                            BUS_DEFSRCLOC());
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
                data.emplace_back(geo->init(helper, cfg));
                res.emplace(name, geo);
            }
        }
        return res;
    }
    BUS_TRACE_END();
}

std::shared_ptr<Sampler> loadSampler(std::shared_ptr<Config> config,
                                     PluginHelper helper,
                                     Bus::ModuleSystem& sys, unsigned msd,
                                     SamplerData& data) {
    sys.getReporter().apply(Bus::ReportLevel::Info, "Loading sampler",
                            BUS_DEFSRCLOC());
    auto sampler =
        sys.instantiateByName<Sampler>(config->attribute("Plugin")->asString());
    data = sampler->init(helper, config, msd);
    return sampler;
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
        PCO.pipelineLaunchParamsVariableName = "launchParam";
        PCO.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;
        PCO.usesMotionBlur = false;
        PCO.numAttributeValues = 0;
        PCO.numPayloadValues = 2;
        std::shared_ptr<PluginHelperAPI> helper =
            buildPluginHelper(context.get(), scenePath, debug, MCO, PCO);
        auto& reporter = sys.getReporter();
        unsigned msd = 0;
        std::vector<OptixProgramGroup> groups;

        CameraData cdata;
        CameraAdapter camera(config->attribute("Camera"), helper.get(), sys,
                             cdata);
        groups.emplace_back(cdata.group);
        msd = std::max(msd, cdata.maxSampleDim);

        IntegratorData idata;
        std::shared_ptr<Integrator> integrator = loadIntegrator(
            config->attribute("Integrator"), helper.get(), sys, idata);
        groups.emplace_back(idata.group);
        msd = std::max(msd, idata.maxSampleDim);

        DriverData ddata;
        std::shared_ptr<Driver> driver =
            loadDriver(config->attribute("Driver"), helper.get(), sys, ddata);
        groups.insert(groups.end(), ddata.group.begin(), ddata.group.end());

        std::vector<GeometryData> gdata;
        GeometryLib geos = loadGeometries(config->attribute("GeometryLib"),
                                          helper.get(), sys, gdata);
        for(auto&& data : gdata) {
            groups.insert(groups.end(), data.group.begin(), data.group.end());
            msd = std::max(msd, data.maxSampleDim);
        }

        SamplerData sdata;
        std::shared_ptr<Sampler> sampler = loadSampler(
            config->attribute("Sampler"), helper.get(), sys, msd, sdata);
        groups.insert(groups.end(), sdata.group.begin(), sdata.group.end());

        OptixPipeline pipe;
        OptixPipelineLinkOptions PLO;
        PLO.debugLevel = MCO.debugLevel;
        PLO.maxTraceDepth = 2;
        PLO.overrideUsesMotionBlur = 0;
        checkOptixError(optixPipelineCreate(
            context.get(), &PCO, &PLO, groups.data(),
            static_cast<unsigned>(groups.size()), nullptr, nullptr, &pipe));
        Pipeline pipeline{ pipe };

        // TODO:optixPipelineSetStackSize

        LaunchParam launchParam;
        launchParam.lightSbtOffset = 0;
        launchParam.samplerSbtOffset =
            static_cast<unsigned>(SBTSlot::userOffset);
        launchParam.root = gdata.front().handle;

        OptixShaderBindingTable sbt;
        std::vector<Data> hitGroup;
        hitGroup.emplace_back(gdata.front().radSBTData);
        hitGroup.emplace_back(gdata.front().occSBTData);
        Buffer hgBuf = uploadSBTRecords(0, hitGroup, sbt.hitgroupRecordBase,
                                        sbt.hitgroupRecordStrideInBytes,
                                        sbt.hitgroupRecordStrideInBytes);

        checkCudaError(cuStreamSynchronize(0));
        reporter.apply(ReportLevel::Info, "Everything is ready.",
                       BUS_DEFSRCLOC());
        struct DriverHelperImpl final : DriverHelperAPI {
        private:
            CameraAdapter& mCamera;
            OptixShaderBindingTable& mSBT;
            std::vector<Data>& mCallable;
            OptixPipeline mPipeline;
            LaunchParam mParam;

        public:
            DriverHelperImpl(CameraAdapter& camera,
                             OptixShaderBindingTable& sbt,
                             std::vector<Data>& callable, OptixPipeline& pipe,
                             LaunchParam param)
                : mCamera(camera), mSBT(sbt), mCallable(callable),
                  mPipeline(pipe), mParam(param) {}
            void doRender(Uint2 size,
                          const std::function<void(OptixShaderBindingTable&)>&
                              callBack) override {
                mCallable[static_cast<unsigned>(SBTSlot::generateRay)] =
                    mCamera.prepare(size);
                Buffer buf =
                    uploadSBTRecords(0, mCallable, mSBT.callablesRecordBase,
                                     mSBT.callablesRecordStrideInBytes,
                                     mSBT.callablesRecordCount);
                callBack(mSBT);
                Buffer param = uploadParam(0, mParam);

                checkOptixError(optixLaunch(mPipeline, 0, asPtr(param),
                                            sizeof(mParam), &mSBT, size.x,
                                            size.y, 1));
                checkCudaError(cuStreamSynchronize(0));
            }
            CUstream getStream() const override {
                return 0;
            }
        };
        std::vector<Data> callables(static_cast<unsigned>(SBTSlot::userOffset));
        callables[static_cast<unsigned>(SBTSlot::samplePixel)] = idata.sbtData;
        callables.insert(callables.end(), sdata.sbtData.begin(),
                         sdata.sbtData.end());
        auto dHelper = std::make_unique<DriverHelperImpl>(
            camera, sbt, callables, pipe, launchParam);
        driver->doRender(dHelper.get());
        checkCudaError(cuCtxSynchronize());
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
