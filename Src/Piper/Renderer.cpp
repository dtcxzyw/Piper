#include "../Shared/CommandAPI.hpp"
#include "../Shared/ConfigAPI.hpp"
#include "../Shared/DriverAPI.hpp"
#include "../Shared/GeometryAPI.hpp"
#include "../Shared/IntegratorAPI.hpp"
#include "../Shared/LightAPI.hpp"
#include "../Shared/LightSamplerAPI.hpp"
#include "../Shared/SamplerAPI.hpp"
#include "CameraAdapter.hpp"
#include <chrono>
#pragma warning(push, 0)
#define NOMINMAX
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

std::unique_ptr<PluginHelperAPI>
buildPluginHelper(OptixDeviceContext context, Bus::ModuleSystem& sys,
                  std::shared_ptr<Config> assCfg, const fs::path& scenePath,
                  bool debug, const OptixModuleCompileOptions& MCO,
                  const OptixPipelineCompileOptions& PCO,
                  std::vector<Data>& cdata, std::vector<Data>& hdata,
                  std::vector<std::shared_ptr<Light>>& lights,
                  std::set<OptixProgramGroup>& group);

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
        case 4:
            level = ReportLevel::Info;
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
        checkCudaError(cuCtxSetCurrent(ctx));
        // checkCudaError(cuCtxCreate(&ctx, CU_CTX_SCHED_AUTO, dev));
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
        OptixDeviceContextOptions opt = {};
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

std::shared_ptr<LightSampler> loadLightSampler(std::shared_ptr<Config> config,
                                               PluginHelper helper,
                                               Bus::ModuleSystem& sys,
                                               size_t lightNum,
                                               LightSamplerData& data) {
    sys.getReporter().apply(Bus::ReportLevel::Info, "Loading light sampler",
                            BUS_DEFSRCLOC());
    auto sampler = sys.instantiateByName<LightSampler>(
        config->attribute("Plugin")->asString());
    data = sampler->init(helper, config, lightNum);
    return sampler;
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
        using Clock = std::chrono::high_resolution_clock;
        auto initTs = Clock::now();
        CUDAContext ctx = createCUDAContext(sys.getReporter());
        auto global = config->attribute("Core");
        Context context = createContext(ctx.get(), sys.getReporter(), global);
        bool debug = global->attribute("Debug")->asBool();
        if(!debug) {
            checkCudaError(cuCtxSetLimit(CU_LIMIT_PRINTF_FIFO_SIZE, 0));
        }

        OptixModuleCompileOptions MCO = {};
        MCO.debugLevel = (debug ? OPTIX_COMPILE_DEBUG_LEVEL_FULL :
                                  OPTIX_COMPILE_DEBUG_LEVEL_NONE);
        MCO.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
        MCO.optLevel = (debug ? OPTIX_COMPILE_OPTIMIZATION_LEVEL_0 :
                                OPTIX_COMPILE_OPTIMIZATION_LEVEL_3);
        OptixPipelineCompileOptions PCO = {};
        PCO.exceptionFlags = OPTIX_EXCEPTION_FLAG_STACK_OVERFLOW |
            OPTIX_EXCEPTION_FLAG_TRACE_DEPTH;
        if(debug)
            PCO.exceptionFlags |= OPTIX_EXCEPTION_FLAG_DEBUG;
        PCO.pipelineLaunchParamsVariableName = "launchParam";
        // TODO:simple graph optimization
        PCO.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;
        // TODO:motion blur
        PCO.usesMotionBlur = false;
        // TODO:configurable attribute
        PCO.numAttributeValues = 2;
        PCO.numPayloadValues = 2;
        std::set<OptixProgramGroup> groups;
        std::vector<Data> callableData, hitGroupData;
        std::vector<std::shared_ptr<Light>> lights;
        std::shared_ptr<PluginHelperAPI> helper = buildPluginHelper(
            context.get(), sys, config->attribute("Assets"), scenePath, debug,
            MCO, PCO, callableData, hitGroupData, lights, groups);

        auto& reporter = sys.getReporter();
        unsigned msd = 0;

        CameraData cdata;
        CameraAdapter camera(config->attribute("Camera"), helper.get(), sys,
                             cdata);
        groups.insert(cdata.group);
        msd = std::max(msd, cdata.maxSampleDim);

        IntegratorData idata;
        std::shared_ptr<Integrator> integrator = loadIntegrator(
            config->attribute("Integrator"), helper.get(), sys, idata);
        groups.insert(idata.group);
        msd = std::max(msd, idata.maxSampleDim);

        DriverData ddata;
        std::shared_ptr<Driver> driver =
            loadDriver(config->attribute("Driver"), helper.get(), sys, ddata);
        groups.insert(ddata.group.begin(), ddata.group.end());

        // TODO:scene analysis
        reporter.apply(ReportLevel::Info, "Loading scene graph",
                       BUS_DEFSRCLOC());
        Bus::FunctionId nodeClass{
            Bus::str2GUID("{9EAF8BBA-3C9B-46B9-971F-1C4F18670F74}"), "Node"
        };
        std::shared_ptr<Geometry> root = sys.instantiate<Geometry>(nodeClass);
        root->init(helper.get(), config->attribute("Scene"));
        msd = std::max(msd, root->getData().maxSampleDim);

        for(auto&& light : lights) {
            auto data = light->getData();
            msd = std::max(msd, data.maxSampleDim);
            groups.insert(data.group);
        }

        // TODO:LightSamplerHelper
        LightSamplerData lsdata;
        std::shared_ptr<LightSampler> lightSampler =
            loadLightSampler(config->attribute("LightSampler"), helper.get(),
                             sys, lights.size(), lsdata);
        groups.insert(lsdata.group);
        msd = std::max(msd, lsdata.maxSampleDim);

        SamplerData sdata;
        std::shared_ptr<Sampler> sampler = loadSampler(
            config->attribute("Sampler"), helper.get(), sys, msd, sdata);
        groups.insert(sdata.group.begin(), sdata.group.end());

        reporter.apply(ReportLevel::Info, "Compiling Pipeline",
                       BUS_DEFSRCLOC());

        std::vector<OptixProgramGroup> linearGroup{ groups.begin(),
                                                    groups.end() };
        OptixPipeline pipe;
        OptixPipelineLinkOptions PLO = {};
        PLO.debugLevel = MCO.debugLevel;
        PLO.maxTraceDepth = 2;
        PLO.overrideUsesMotionBlur = 0;
        checkOptixError(
            optixPipelineCreate(context.get(), &PCO, &PLO, linearGroup.data(),
                                static_cast<unsigned>(linearGroup.size()),
                                nullptr, nullptr, &pipe));
        Pipeline pipeline{ pipe };

        // TODO:optixPipelineSetStackSize

        LaunchParam launchParam;
        launchParam.samplerSbtOffset =
            static_cast<unsigned>(SBTSlot::userOffset) +
            static_cast<unsigned>(callableData.size());
        launchParam.lightSbtOffset = launchParam.samplerSbtOffset + msd;
        launchParam.root = root->getData().handle;

        OptixShaderBindingTable sbt = {};
        Buffer hgBuf = uploadSBTRecords(0, hitGroupData, sbt.hitgroupRecordBase,
                                        sbt.hitgroupRecordStrideInBytes,
                                        sbt.hitgroupRecordCount);

        checkCudaError(cuStreamSynchronize(0));

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
                BUS_TRACE_BEG() {
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
                BUS_TRACE_END();
            }
            CUstream getStream() const override {
                return 0;
            }
        };
        std::vector<Data> callables(static_cast<unsigned>(SBTSlot::userOffset));
        callables[static_cast<unsigned>(SBTSlot::samplePixel)] = idata.sbtData;
        callables[static_cast<unsigned>(SBTSlot::sampleOneLight)] =
            lsdata.sbtData;
        callables.insert(callables.end(), callableData.begin(),
                         callableData.end());
        callables.insert(callables.end(), sdata.sbtData.begin(),
                         sdata.sbtData.end());
        for(auto&& light : lights)
            callables.push_back(light->getData().sbtData);
        auto dHelper = std::make_unique<DriverHelperImpl>(
            camera, sbt, callables, pipe, launchParam);
        reporter.apply(ReportLevel::Info, "Everything is ready.",
                       BUS_DEFSRCLOC());
        auto renderTs = Clock::now();
        driver->doRender(dHelper.get());
        checkCudaError(cuCtxSynchronize());
        auto endTs = Clock::now();
        {
            auto format = [](uint64_t t) {
                uint64_t msp = t % 1000;
                t /= 1000;
                uint64_t sp = t % 60;
                t /= 60;
                uint64_t mp = t % 60;
                t /= 60;
                std::string res;
                bool flag = false;
                if(t)
                    res += std::to_string(t) + " h ", flag = true;
                if(mp || flag)
                    res += std::to_string(mp) + " min ", flag = true;
                if(sp || flag)
                    res += std::to_string(sp) + " s ";
                res += std::to_string(msp) + " ms";
                return res;
            };
            auto initTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(renderTs -
                                                                      initTs)
                    .count();
            auto renderTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(endTs -
                                                                      renderTs)
                    .count();
            reporter.apply(ReportLevel::Info,
                           "init time:" + format(initTime) +
                               ",render time:" + format(renderTime),
                           BUS_DEFSRCLOC());
        }
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
