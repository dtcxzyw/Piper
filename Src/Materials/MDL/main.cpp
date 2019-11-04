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

struct Context final {
    Bus::Reporter& reporter;
    MDL::IMdl_compiler* compiler;
    MDL::ITransaction* transaction;
    MDL::IMdl_factory* factory;
    Handle<MDL::IMdl_execution_context> execContext;
    Context(Bus::Reporter& reporter, MDL::IMdl_compiler* compiler,
            MDL::ITransaction* transaction, MDL::IMdl_factory* factory,
            MDL::IMdl_execution_context* execContext)
        : reporter(reporter), compiler(compiler), transaction(transaction),
          factory(factory), execContext(execContext) {}
    ~Context() {
        printMessages(reporter, execContext.get());
    }
};

Context getContext(Bus::ModuleInstance& inst);

// TODO:EDF and Automatic derivatives Support
class MDLMaterial final : public Material {
private:
    Buffer mArgData;
    ProgramGroup mGroup;
    MaterialData mData;

public:
    explicit MDLMaterial(Bus::ModuleInstance& instance) : Material(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            auto moduleName = config->attribute("Module")->asString();
            auto materialName = config->attribute("Material")->asString();
            auto context = getContext(mInstance);
            checkMDLErrorNNG(context.compiler->load_module(
                context.transaction, moduleName.c_str(),
                context.execContext.get()));
            moduleName = "mdl" + moduleName;
            materialName = moduleName + "::" + materialName;
            Handle<const MDL::IMaterial_definition> def{
                context.transaction->access<MDL::IMaterial_definition>(
                    materialName.c_str())
            };
            if(!def.is_valid_interface())
                BUS_TRACE_THROW(std::logic_error("No material named \"" +
                                                 materialName + "\""));
            BUS_TRACE_POINT();
            Handle<MDL::IValue_factory> valueFactory{
                context.factory->create_value_factory(context.transaction)
            };
            Handle<MDL::IExpression_factory> exprFactory{
                context.factory->create_expression_factory(context.transaction)
            };
            mi::Sint32 ret = 0;
            // TODO:arguments
            Handle<MDL::IMaterial_instance> inst{ def->create_material_instance(
                nullptr, &ret) };
            if(ret != 0)
                BUS_TRACE_THROW(std::runtime_error(
                    "Failed to create material instance. error code=" +
                    std::to_string(ret)));
            // TODO:class compilation
            mi::Uint32 flags = MDL::IMaterial_instance::DEFAULT_OPTIONS;
            Handle<MDL::ICompiled_material> mat{ inst->create_compiled_material(
                flags, context.execContext.get()) };
            Handle<MDL::IMdl_backend> backend{ context.compiler->get_backend(
                MDL::IMdl_compiler::MB_CUDA_PTX) };
            checkMDLErrorEQ(backend->set_option("num_texture_results", "0"));
            checkMDLErrorEQ(
                backend->set_option("tex_lookup_call_mode", "direct_call"));
            // TODO:enable_ro_segment
            // TODO:translate path
            BUS_TRACE_POINT();
            Handle<const MDL::ITarget_code> code(backend->translate_material_df(
                context.transaction, mat.get(), "surface.scattering", "bsdf",
                context.execContext.get()));
            OptixProgramGroupDesc desc = {};
            desc.flags = 0;
            desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
            desc.callables.entryFunctionNameCC =
                "__continuation_callable__sample";
            // TODO:render state usage
            {
                auto usage = code->get_render_state_usage();
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
                reporter().apply(ReportLevel::Debug, "Usage:" + msg,
                                 BUS_DEFSRCLOC());
            }
            BUS_TRACE_POINT();
            // TODO:merge PTX
            auto samplePTX = loadPTX(modulePath().parent_path() / "Kernel.ptx");
            std::string mdlPTX = code->get_code();
            mdlPTX = mdlPTX.substr(mdlPTX.find(".address_size") + 16);
            auto header =
                samplePTX.substr(0, samplePTX.find(".address_size") + 16);
            auto kernel = samplePTX.substr(
                samplePTX.find(".extern .const .align 8 .b8 launchParam[16]"));
            auto finalPTX = header + mdlPTX + kernel;
            // reporter().apply(ReportLevel::Debug, finalPTX, BUS_DEFSRCLOC());
            desc.callables.moduleCC = helper->loadModuleFromPTX(finalPTX);

            BUS_TRACE_POINT();

            OptixProgramGroupOptions opt = {};
            OptixProgramGroup group;
            checkOptixError(optixProgramGroupCreate(helper->getContext(), &desc,
                                                    1, &opt, nullptr, nullptr,
                                                    &group));
            mGroup.reset(group);
            // All DF_* functions of one material DF use the same target
            // argument block.
            auto argID = code->get_callable_function_argument_block_index(0);
            if(argID != ~mi::Size(0)) {
                Handle<const MDL::ITarget_argument_block> argBlock{
                    code->get_argument_block(argID)
                };
                mArgData =
                    uploadData(0, argBlock->get_data(), argBlock->get_size());
                checkCudaError(cuStreamSynchronize(0));
            }
            DataDesc data;
            data.argData = static_cast<const char*>(mArgData.get());
            mData.group = group;
            mData.maxSampleDim = 3;
            mData.radData = packSBTRecord(group, data);
        }
        BUS_TRACE_END();
    }
    MaterialData getData() override {
        return mData;
    }
};

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
        return Context(getSystem().getReporter(), mCompiler.get(),
                       mTransaction.get(), mFactory.get(),
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
