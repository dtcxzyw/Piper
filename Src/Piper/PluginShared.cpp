#include "../Shared/PluginShared.hpp"
#include "../Shared/ConfigAPI.hpp"
#include <fstream>
#include <sstream>
#include <unordered_map>
#pragma warning(push, 0)
#include <nvrtc.h>
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.Builtin.PluginHelper");

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

struct ModuleDeleter final {
    void operator()(OptixModule mod) const {
        checkOptixError(optixModuleDestroy(mod));
    }
};

using Module = std::unique_ptr<OptixModule_t, ModuleDeleter>;

class PluginHelperImpl final : public PluginHelperAPI {
private:
    OptixDeviceContext mContext;
    fs::path mScenePath;
    bool mDebug;
    OptixModuleCompileOptions mMCO;
    OptixPipelineCompileOptions mPCO;
    std::vector<Data>& mCData;
    std::set<OptixProgramGroup>& mCGroup;
    std::unordered_map<std::string, Module> mModules;

public:
    PluginHelperImpl(OptixDeviceContext context, const fs::path& scenePath,
                     bool debug, const OptixModuleCompileOptions& MCO,
                     const OptixPipelineCompileOptions& PCO,
                     std::vector<Data>& cdata,
                     std::set<OptixProgramGroup>& cgroup)
        : mContext(context), mScenePath(scenePath), mDebug(debug), mMCO(MCO),
          mPCO(PCO), mCData(cdata), mCGroup(cgroup) {}
    unsigned addCallable(OptixProgramGroup group, const Data& sbtData) {
        mCGroup.insert(group);
        unsigned res = static_cast<unsigned>(mCData.size()) +
            static_cast<unsigned>(SBTSlot::userOffset);
        mCData.push_back(sbtData);
        return res;
    }
    OptixDeviceContext getContext() const override {
        return mContext;
    }
    fs::path scenePath() const override {
        return mScenePath;
    }
    bool isDebug() const override {
        return mDebug;
    }
    OptixModule loadModuleFromPTX(const std::string& ptx) override {
        BUS_TRACE_BEG() {
            Module& mod = mModules[ptx];
            if(!mod) {
                OptixModule handle;
                checkOptixError(optixModuleCreateFromPTX(
                    mContext, &mMCO, &mPCO, ptx.c_str(), ptx.size(), nullptr,
                    nullptr, &handle));
                mod.reset(handle);
            }
            return mod.get();
        }
        BUS_TRACE_END();
    }
    OptixModule loadModuleFromFile(const fs::path& file) override {
        return loadModuleFromPTX(loadPTX(file));
    }
    OptixModule loadModuleFromSrc(const std::string& src) override;
};

std::unique_ptr<PluginHelperAPI>
buildPluginHelper(OptixDeviceContext context, const fs::path& scenePath,
                  bool debug, const OptixModuleCompileOptions& MCO,
                  const OptixPipelineCompileOptions& PCO,
                  std::vector<Data>& cdata,
                  std::set<OptixProgramGroup>& cgroup) {
    return std::make_unique<PluginHelperImpl>(context, scenePath, debug, MCO,
                                              PCO, cdata, cgroup);
}

static void checkNVRTCError(nvrtcResult res) {
    if(res != NVRTC_SUCCESS)
        throw std::runtime_error(std::string("NVRTCError") +
                                 nvrtcGetErrorString(res));
}

struct NVRTCProgramDeleter final {
    void operator()(nvrtcProgram prog) {
        checkNVRTCError(nvrtcDestroyProgram(&prog));
    }
};

static const char* header = R"#()#";

OptixModule PluginHelperImpl::loadModuleFromSrc(const std::string& src) {
    BUS_TRACE_BEG() {
        // TODO:PTX Caching
        using Program = std::unique_ptr<_nvrtcProgram, NVRTCProgramDeleter>;
        Program program;
        nvrtcProgram prog;
        const char* headerName = "runtime.hpp";
        checkNVRTCError(nvrtcCreateProgram(&prog, src.c_str(), "kernel.cu", 1,
                                           &header, &headerName));
        program.reset(prog);
        const char* opt[] = { "-use_fast_math", "-default-device",
                              "-rdc=true" };
        try {
            checkNVRTCError(nvrtcCompileProgram(
                program.get(), static_cast<int>(std::size(opt)), opt));
        } catch(...) {
            size_t siz;
            if(nvrtcGetProgramLogSize(program.get(), &siz) == NVRTC_SUCCESS) {
                std::string logStr(siz, '@');
                if(nvrtcGetProgramLog(program.get(), logStr.data()) ==
                   NVRTC_SUCCESS) {
                    std::throw_with_nested(
                        std::runtime_error("Compile Log:" + logStr));
                }
            }
            throw;
        }
        size_t siz;
        checkNVRTCError(nvrtcGetPTXSize(program.get(), &siz));
        std::string ptx(siz, '#');
        checkNVRTCError(nvrtcGetPTX(program.get(), ptx.data()));
        return loadModuleFromPTX(ptx);
    }
    BUS_TRACE_END();
}
