#include "../Shared/PluginShared.hpp"
#include "../Shared/ConfigAPI.hpp"
#include <fstream>
#include <sstream>
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

class PluginHelperImpl final : public PluginHelperAPI {
private:
    OptixDeviceContext mContext;
    fs::path mScenePath;
    bool mDebug;
    OptixModuleCompileOptions mMCO;
    OptixPipelineCompileOptions mPCO;
    std::vector<Data>& mCData;
    std::set<OptixProgramGroup>& mCGroup;

public:
    PluginHelperImpl(OptixDeviceContext context, const fs::path& scenePath,
                     bool debug, const OptixModuleCompileOptions& MCO,
                     const OptixPipelineCompileOptions& PCO,
                     std::vector<Data>& cdata,
                     std::set<OptixProgramGroup>& cgroup)
        : mContext(context), mScenePath(scenePath), mDebug(debug), mMCO(MCO),
          mPCO(PCO), mCData(cdata), mCGroup(cgroup) {}
    unsigned addCallable(OptixProgramGroup group, const Data& sbtData) const {
        mGroup.insert(group);
        unsigned res = static_cast<unsigned>(mSBTData.size()) +
            static_cast<unsigned>(SBTSlot::userOffset);
        mSBTData.push_back(sbtData);
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
    Module compile(const std::string& ptx) const override {
        BUS_TRACE_BEG() {
            OptixModule mod;
            checkOptixError(optixModuleCreateFromPTX(mContext, &mMCO, &mPCO,
                                                     ptx.c_str(), ptx.size(),
                                                     nullptr, nullptr, &mod));
            return Module{ mod };
        }
        BUS_TRACE_END();
    }
    Module compileFile(const fs::path& file) const override {
        return compile(loadPTX(file));
    }
    std::string compileSource(const std::string& src) const override;
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

std::string PluginHelperImpl::compileSource(const std::string& src) const {
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
        return ptx;
    }
    BUS_TRACE_END();
}
