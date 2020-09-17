#include "../../Shared/ConfigAPI.hpp"
#include "../../Shared/MaterialAPI.hpp"
#include "../../Shared/TextureSamplerAPI.hpp"
#pragma warning(push, 0)
#include "MDLShared.hpp"
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)
#include "DataDesc.hpp"
#include <cstdlib>
#include <fstream>

BUS_MODULE_NAME("Piper.BuiltinMaterial.MDL");

static std::string loadPTX(const fs::path& path) {
    BUS_TRACE_BEG() {
        auto size = fs::file_size(path);
        std::ifstream in(path, std::ios::in | std::ios::binary);
        std::string res(size, '#');
        in.read(res.data(), size);
        return res;
    }
    BUS_TRACE_END();
}

Context getContext(Bus::ModuleInstance& inst);

class MDLCUDAHelper : private Unmoveable {
public:
    virtual std::string genPTX() = 0;
    virtual DataDesc getData() = 0;
    virtual mi::Uint32 getUsage() = 0;
};

std::shared_ptr<MDLCUDAHelper>
getHelper(Context& context, const std::string& module, const std::string& mat);

// TODO:EDF
// TODO:Automatic derivatives Support
class MDLMaterial final : public Material {
private:
    ProgramGroup mGroup;
    MaterialData mData;
    std::shared_ptr<MDLCUDAHelper> mHelper;

public:
    explicit MDLMaterial(Bus::ModuleInstance& instance) : Material(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            auto moduleName = config->attribute("Module")->asString();
            auto materialName = config->attribute("Material")->asString();
            materialName = moduleName + "::" + materialName;
            auto context = getContext(mInstance);
            BUS_TRACE_POINT();
            // TODO:arguments
            mHelper = getHelper(context, moduleName, materialName);

            // TODO:render state usage
            {
                auto usage = mHelper->getUsage();
                std::string msg;
                using Usage = MDL::ITarget_code::State_usage_property;
#define CHECKUSAGE(x) \
    if(usage & x)     \
        msg += #x "\n";
                CHECKUSAGE(Usage::SU_ANIMATION_TIME);
                CHECKUSAGE(Usage::SU_DIRECTION);
                CHECKUSAGE(Usage::SU_GEOMETRY_NORMAL);
                CHECKUSAGE(Usage::SU_GEOMETRY_TANGENTS);
                CHECKUSAGE(Usage::SU_MOTION);
                CHECKUSAGE(Usage::SU_NORMAL);
                CHECKUSAGE(Usage::SU_OBJECT_ID);
                CHECKUSAGE(Usage::SU_POSITION);
                CHECKUSAGE(Usage::SU_ROUNDED_CORNER_NORMAL);
                CHECKUSAGE(Usage::SU_TANGENT_SPACE);
                CHECKUSAGE(Usage::SU_TEXTURE_COORDINATE);
                CHECKUSAGE(Usage::SU_TEXTURE_TANGENTS);
                CHECKUSAGE(Usage::SU_TRANSFORMS);
#undef CHECKUSAGE
                reporter().apply(ReportLevel::Debug, "Usage:\n" + msg,
                                 BUS_DEFSRCLOC());
            }

            OptixProgramGroupDesc desc = {};
            desc.flags = 0;
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            BUS_TRACE_POINT();
            // TODO:link PTX
            auto samplePTX = loadPTX(modulePath().parent_path() / "Kernel.ptx");
            std::string mdlPTX = mHelper->genPTX();
            // remove .extern
            while(true) {
                size_t pos = mdlPTX.find(".extern .func");
                if(pos == mdlPTX.npos)
                    break;
                size_t end = mdlPTX.find(';', pos);
                mdlPTX = mdlPTX.substr(0, pos) + mdlPTX.substr(end + 1);
            }
            mdlPTX = mdlPTX.substr(mdlPTX.find(".address_size") + 16);
            auto header =
                samplePTX.substr(0, samplePTX.find(".address_size") + 16);
            auto funcDef = samplePTX.substr(
                samplePTX.find(".extern .const .align 8 .b8 launchParam[16];"));
            size_t cutPos =
                funcDef.find(".visible .func __continuation_callable__sample");
            auto texFunc = funcDef.substr(0, cutPos);
            auto kernel = funcDef.substr(cutPos);
            // BUG:doesn't support printf
            auto finalPTX = header + '\n' + texFunc + mdlPTX + kernel;
            // reporter().apply(ReportLevel::Debug, finalPTX, BUS_DEFSRCLOC());
            std::hash<std::string> hasher;
            const ModuleDesc& mod = helper->getModuleManager()->getModule(
                BUS_DEFAULT_MODULE_NAME + std::to_string(hasher(finalPTX)),
                [&] { return finalPTX; });
            desc.callables.moduleCC = mod.handle.get();
            desc.callables.entryFunctionNameCC =
                mod.map("__continuation_callable__sample");

            BUS_TRACE_POINT();

            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mGroup.reset(group);
            DataDesc data = mHelper->getData();
            mData.group = group;
            mData.maxSampleDim = 4;
            mData.radData = packSBTRecord(group, data);
            mData.dss = 0;
            OptixStackSizes size;
            checkOptixError(optixProgramGroupGetStackSize(group, &size));
            mData.css = size.cssCC;
        }
        BUS_TRACE_END();
    }
    MaterialData getData() override {
        return mData;
    }
};

// TODO:set ILogger
// TODO:use IImage_api for image I/O
class Instance final : public Bus::ModuleInstance {
private:
    HMODULE mHandle;
    Handle<MDL::INeuray> mNeuray;
    Handle<MDL::IMdl_compiler> mCompiler;
    Handle<MDL::IDatabase> mDataBase;
    Handle<MDL::IScope> mScope;
    Handle<MDL::ITransaction> mTransaction;
    Handle<MDL::IMdl_factory> mFactory;

public:
    Instance(const fs::path& path, Bus::ModuleSystem& sys)
        : Bus::ModuleInstance(path, sys) {
        BUS_TRACE_BEG() {
            checkOptixError(optixInit());
            mNeuray = MDLInit(mHandle, path, sys.getReporter());
            if(!mNeuray)
                BUS_TRACE_THROW(
                    std::runtime_error("Failed to initialize MDL."));
            mCompiler = mNeuray->get_api_component<MDL::IMdl_compiler>();
            // configure
            {
                // texture
                auto nvfreeimage =
                    (path.parent_path() / "nv_freeimage" MI_BASE_DLL_FILE_EXT)
                        .string();
                checkMDLErrorEQ(
                    mCompiler->load_plugin_library(nvfreeimage.c_str()));
            }
            {
                // core_definitions
                auto core = path.parent_path().string();
                checkMDLErrorEQ(mCompiler->add_module_path(core.c_str()));
            }
            {
                // vMaterial
                const char* modPath = getenv("MDL_USER_PATH");
                if(modPath) {
                    checkMDLErrorEQ(mCompiler->add_module_path(modPath));
                    sys.getReporter().apply(
                        ReportLevel::Info,
                        std::string(
                            "Use the system's material library.[path=") +
                            modPath + "]",
                        BUS_DEFSRCLOC());
                }
            }
            mi::Sint32 res = mNeuray->start(true);
            if(res)
                BUS_TRACE_THROW(std::runtime_error(
                    "Failed to start MDL neuray. error code=" +
                    std::to_string(res)));
            mDataBase = mNeuray->get_api_component<MDL::IDatabase>();
            mScope = mDataBase->get_global_scope();
            mTransaction = mScope->create_transaction();
            mFactory = mNeuray->get_api_component<MDL::IMdl_factory>();
        }
        BUS_TRACE_END();
    }
    ~Instance() {
        mFactory.reset();
        mTransaction->commit();
        mTransaction.reset();
        mScope.reset();
        mDataBase.reset();
        mCompiler.reset();
        if(mNeuray->shutdown(true) != 0)
            getSystem().getReporter().apply(
                ReportLevel::Error,
                "Failed to shutdown neuray: ", BUS_DEFSRCLOC());
        mNeuray.reset();
        MDLUninit(mHandle, getSystem().getReporter());
    }
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = BUS_DEFAULT_MODULE_NAME;
        res.guid = Bus::str2GUID("{36FBEACF-0545-4560-8CA2-997AF736D064}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "NVIDIA Material Definition Language";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Material::getInterface())
            return { "MDLMaterial" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "MDLMaterial")
            return std::make_shared<MDLMaterial>(*this);
        return nullptr;
    }
    Context getContext() {
        return Context(getSystem().getReporter(), mNeuray.get(),
                       mCompiler.get(), mTransaction.get(), mFactory.get(),
                       mFactory->create_execution_context());
    }
};

static Context getContext(Bus::ModuleInstance& inst) {
    return dynamic_cast<Instance&>(inst).getContext();
}

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
