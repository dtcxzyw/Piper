#include "../Shared/PluginShared.hpp"
#include "../Shared/ConfigAPI.hpp"
#include <fstream>
#include <sstream>
#include <unordered_map>
#pragma warning(push, 0)
#include <optix_stubs.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.Builtin.PluginHelper");

static std::string loadStr(const fs::path& path) {
    BUS_TRACE_BEG() {
        auto size = fs::file_size(path);
        std::ifstream in(path, std::ios::in | std::ios::binary);
        std::string res(size, '#');
        in.read(res.data(), size);
        return res;
    }
    BUS_TRACE_END();
}

static Data loadData(const fs::path& path) {
    BUS_TRACE_BEG() {
        static_assert(sizeof(char) == sizeof(std::byte));
        auto size = fs::file_size(path);
        std::ifstream in(path, std::ios::in | std::ios::binary);
        Data res(size);
        in.read(reinterpret_cast<char*>(res.data()), size);
        return res;
    }
    BUS_TRACE_END();
}

static void remap(std::string& ptx, std::map<std::string, std::string>& map,
                  const std::string& suffix) {
    BUS_TRACE_BEG() {
        std::vector<std::string> funcName;
        std::stringstream ss;
        ss << ptx;
        std::string line;
        std::string base = ".visible .func";
        while(std::getline(ss, line)) {
            if(line.substr(0, base.size()) == base) {
                std::string info = line.substr(base.size());
                if(info.find(".param") != info.npos) {
                    auto pos = info.find_first_of(')');
                    info = info.substr(pos);
                }
                while(info.size() &&
                      !(info.front() == '_' || isalnum(info.front())))
                    info.erase(info.begin());
                while(info.size() &&
                      !(info.back() == '_' || isalnum(info.back())))
                    info.pop_back();
                funcName.push_back(info);
            }
        }
        for(auto&& id : funcName) {
            // TODO:automaton/regex_replace
            std::string rep = id + suffix;
            size_t last = 0;
            do {
                auto pos = ptx.find(id, last + 1);
                if(pos == ptx.npos)
                    break;
                last = pos;
                size_t nxt = pos + id.size();
                if(nxt != ptx.size() && (ptx[nxt] == '_' || isalnum(ptx[nxt])))
                    continue;
                ptx = ptx.substr(0, pos) + rep + ptx.substr(nxt);
            } while(true);
            map[id] = id + suffix;
        }
    }
    BUS_TRACE_END();
}

class ModuleManagerImpl : public ModuleManagerAPI {
private:
    OptixDeviceContext mContext;
    std::unordered_map<std::string, ModuleDesc> mModules;
    OptixModuleCompileOptions mMCO;
    OptixPipelineCompileOptions mPCO;
    std::string mKernelInclude;
    Data mLibDevice;

public:
    ModuleManagerImpl(OptixDeviceContext context,
                      const OptixModuleCompileOptions& MCO,
                      const OptixPipelineCompileOptions& PCO)
        : mContext(context), mMCO(MCO), mPCO(PCO) {
        // TODO:compress KernelInclude.hpp
        mKernelInclude = loadStr("KernelInclude.hpp");
        auto removeCRT = [](std::string& str) {
            auto beg = str.find("_CRT_BEGIN_C_HEADER");
            if(beg == str.npos)
                return false;
            auto end = str.find("_CRT_END_C_HEADER");
            str = str.substr(0, beg) + str.substr(end + 17);
            return true;
        };
        while(removeCRT(mKernelInclude))
            ;
        /*
        //Debug Output
        std::ofstream out("out.hpp");
        out << mKernelInclude << std::endl;
        */
        mLibDevice = loadData("libdevice.10.bc");
    }
    const ModuleDesc&
    getModule(const std::string& id,
              const std::function<std::string()>& ptxGen) override {
        BUS_TRACE_BEG() {
            // TODO:ptx cache
            ModuleDesc& mod = mModules[id];
            if(!mod.handle) {
                auto ptx = ptxGen();
                std::hash<std::string> hasher;
                std::stringstream ss;
                ss << "_" << std::hex << std::uppercase << hasher(id);
                remap(ptx, mod.nameMap, ss.str());
                OptixModule handle;
                checkOptixError(optixModuleCreateFromPTX(
                    mContext, &mMCO, &mPCO, ptx.c_str(), ptx.size(), nullptr,
                    nullptr, &handle));
                mod.handle.reset(handle);
            }
            return mod;
        }
        BUS_TRACE_END();
    }
    const ModuleDesc& getModuleFromFile(const fs::path& file) override {
        BUS_TRACE_BEG() {
            return getModule(file.string(), [file] { return loadStr(file); });
        }
        BUS_TRACE_END();
    }
    std::string compileSrc(const std::string& src) override;
    std::string compileNVVMIR(const std::vector<Data>& bc) override;
    Data linkPTX(const std::vector<std::string>& ptx) override;
    std::string cubin2PTX(const Data& cubin) override;
};

// TODO:Divide functions
// ModuleManager
// AssetManager
// ComputeManager
// PipelineManager
class PluginHelperImpl final : public PluginHelperAPI {
private:
    OptixDeviceContext mContext;
    Bus::ModuleSystem& mSys;
    fs::path mScenePath;
    bool mDebug;
    std::vector<Data>& mCData;
    std::vector<Data>& mHData;
    std::set<OptixProgramGroup>& mGroups;
    std::vector<std::shared_ptr<Light>>& mLights;
    std::unordered_map<std::string, std::shared_ptr<Asset>> mAssets;
    std::unordered_map<std::string, std::shared_ptr<Config>> mAssetConfig;
    std::unique_ptr<ModuleManagerImpl> mModuleManager;

    std::shared_ptr<Asset>
    instantiateAssetImpl(Name api, std::shared_ptr<Config> cfg) override {
        BUS_TRACE_BEG() {
            std::string pluginName = cfg->attribute("Plugin")->asString();
            auto res = mSys.parse(pluginName, api);
            Bus::FunctionId fid{ res.first, res.second };
            if(cfg->hasAttr("AssetName")) {
                auto assetName = cfg->attribute("AssetName")->asString();
                auto iter = mAssetConfig.find(assetName);
                if(iter == mAssetConfig.end())
                    BUS_TRACE_THROW(std::logic_error("No asset is named \"" +
                                                     assetName + "\"."));
                auto&& assetCfg = iter->second;
                auto key = assetName + "@" + Bus::GUID2Str(fid.guid) + "#" +
                    fid.name.data();
                std::shared_ptr<Asset>& asset = mAssets[key];
                if(!asset) {
                    asset = mSys.instantiate<Asset>(fid);
                    asset->init(this, assetCfg);
                }
                return asset;
            } else {
                auto inst = mSys.instantiate<Asset>(fid);
                inst->init(this, cfg);
                return inst;
            }
        }
        BUS_TRACE_END();
    }

public:
    PluginHelperImpl(OptixDeviceContext context, Bus::ModuleSystem& sys,
                     std::shared_ptr<Config> assCfg, const fs::path& scenePath,
                     bool debug, const OptixModuleCompileOptions& MCO,
                     const OptixPipelineCompileOptions& PCO,
                     std::vector<Data>& cdata, std::vector<Data>& hdata,
                     std::vector<std::shared_ptr<Light>>& lights,
                     std::set<OptixProgramGroup>& group)
        : mContext(context), mSys(sys), mScenePath(scenePath), mDebug(debug),
          mCData(cdata), mHData(hdata), mLights(lights), mGroups(group) {
        for(auto&& asset : assCfg->expand()) {
            mAssetConfig[asset->attribute("Name")->asString()] = asset;
        }
        mModuleManager = std::make_unique<ModuleManagerImpl>(context, MCO, PCO);
    }
    ModuleManager getModuleManager() override {
        return mModuleManager.get();
    }
    unsigned addCallable(OptixProgramGroup group,
                         const Data& sbtData) override {
        mGroups.insert(group);
        unsigned res = static_cast<unsigned>(mCData.size()) +
            static_cast<unsigned>(SBTSlot::userOffset);
        mCData.push_back(sbtData);
        return res;
    }
    unsigned addHitGroup(OptixProgramGroup radGroup, const Data& rad,
                         OptixProgramGroup occGroup, const Data& occ) override {
        mGroups.insert(radGroup);
        mGroups.insert(occGroup);
        // TODO:merge same hit group
        unsigned res = static_cast<unsigned>(mHData.size()) / 2;
        mHData.push_back(rad);
        mHData.push_back(occ);
        return res;
    }
    void addLight(std::shared_ptr<Light> light) override {
        mLights.push_back(light);
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
};

std::unique_ptr<PluginHelperAPI>
buildPluginHelper(OptixDeviceContext context, Bus::ModuleSystem& sys,
                  std::shared_ptr<Config> assCfg, const fs::path& scenePath,
                  bool debug, const OptixModuleCompileOptions& MCO,
                  const OptixPipelineCompileOptions& PCO,
                  std::vector<Data>& cdata, std::vector<Data>& hdata,
                  std::vector<std::shared_ptr<Light>>& lights,
                  std::set<OptixProgramGroup>& group) {
    return std::make_unique<PluginHelperImpl>(context, sys, assCfg, scenePath,
                                              debug, MCO, PCO, cdata, hdata,
                                              lights, group);
}

#pragma warning(push, 0)
#include <nvrtc.h>
#pragma warning(pop)

static void checkNVRTCError(nvrtcResult res) {
    if(res != NVRTC_SUCCESS)
        throw std::runtime_error(std::string("[NVRTCError]") +
                                 nvrtcGetErrorString(res));
}

struct NVRTCProgramDeleter final {
    void operator()(nvrtcProgram prog) {
        checkNVRTCError(nvrtcDestroyProgram(&prog));
    }
};

std::string ModuleManagerImpl::compileSrc(const std::string& src) {
    BUS_TRACE_BEGIN("Piper.Builtin.PluginHelper.NVRTC") {
        // TODO:PTX Caching
        using Program = std::unique_ptr<_nvrtcProgram, NVRTCProgramDeleter>;
        Program program;
        nvrtcProgram prog;
        const char* headerName = "KernelInclude.hpp";
        const char* header = mKernelInclude.c_str();
        checkNVRTCError(nvrtcCreateProgram(&prog, src.c_str(), "kernel.cu", 1,
                                           &header, &headerName));
        program.reset(prog);
        // TODO:optimization level
        const char* opt[] = { "-use_fast_math", "-default-device", "-rdc=true",
                              "-w", "-std=c++14" };
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

#pragma warning(push, 0)
#include <nvvm.h>
#pragma warning(pop)

static void checkNVVMError(nvvmResult res) {
    if(res != NVVM_SUCCESS)
        throw std::runtime_error(std::string("[NVVMError]") +
                                 nvvmGetErrorString(res));
}

struct NVVMProgramDeleter final {
    void operator()(nvvmProgram prog) {
        checkNVVMError(nvvmDestroyProgram(&prog));
    }
};

std::string ModuleManagerImpl::compileNVVMIR(const std::vector<Data>& bitcode) {
    BUS_TRACE_BEGIN("Piper.Builtin.PluginHelper.NVVM") {
        using Program = std::unique_ptr<_nvvmProgram, NVVMProgramDeleter>;
        Program program;
        nvvmProgram prog;
        checkNVVMError(nvvmCreateProgram(&prog));
        program.reset(prog);
        for(auto&& bc : bitcode) {
            checkNVVMError(nvvmAddModuleToProgram(
                prog, reinterpret_cast<const char*>(bc.data()), bc.size(),
                nullptr));
        }
        checkNVVMError(nvvmLazyAddModuleToProgram(
            prog, reinterpret_cast<const char*>(mLibDevice.data()),
            mLibDevice.size(), "libdevice"));
        // TODO:optimization level
        const char* opt[] = { "-prec-div=0", "-prec-sqrt=0" };
        try {
            checkNVVMError(nvvmCompileProgram(
                prog, static_cast<int>(std::size(opt)), opt));
        } catch(...) {
            size_t siz;
            if(nvvmGetProgramLogSize(prog, &siz) == NVVM_SUCCESS) {
                std::string logStr(siz, '@');
                if(nvvmGetProgramLog(prog, logStr.data()) == NVVM_SUCCESS) {
                    std::throw_with_nested(
                        std::runtime_error("Compile Log:" + logStr));
                }
            }
            throw;
        }
        size_t siz;
        checkNVVMError(nvvmGetCompiledResultSize(prog, &siz));
        std::string ptx(siz, '#');
        checkNVVMError(nvvmGetCompiledResult(program.get(), ptx.data()));
        return ptx;
    }
    BUS_TRACE_END();
}

struct LinkerDeleter final {
    void operator()(CUlinkState prog) {
        checkCudaError(cuLinkDestroy(prog));
    }
};

Data ModuleManagerImpl::linkPTX(const std::vector<std::string>& ptx) {
    BUS_TRACE_BEGIN("Piper.Builtin.PluginHelper.JITLinker") {
        using Linker = std::unique_ptr<CUlinkState_st, LinkerDeleter>;
        Linker linker;
        CUlinkState state;
        // TODO:CUjit_option op[] = {};
        checkCudaError(cuLinkCreate(0, nullptr, nullptr, &state));
        linker.reset(state);
        for(auto&& src : ptx) {
            // TODO:other input types
            // TODO:data name
            checkCudaError(cuLinkAddData(
                state, CU_JIT_INPUT_PTX, const_cast<char*>(src.c_str()),
                src.size(), nullptr, 0, nullptr, nullptr));
        }
        Data::value_type* cubinPtr;
        size_t size;
        checkCudaError(
            cuLinkComplete(state, reinterpret_cast<void**>(&cubinPtr), &size));
        return Data(cubinPtr, cubinPtr + size);
    }
    BUS_TRACE_END();
}

std::string ModuleManagerImpl::cubin2PTX(const Data& cubin) {
    // use cuobjdump
    BUS_TRACE_BEGIN("Piper.Builtin.PluginHelper.cuobjdump") {
        BUS_TRACE_THROW(std::logic_error("unimplemented feature"));
    }
    BUS_TRACE_END();
}
