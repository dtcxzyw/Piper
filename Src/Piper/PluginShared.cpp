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
    Bus::ModuleSystem& mSys;
    fs::path mScenePath;
    bool mDebug;
    OptixModuleCompileOptions mMCO;
    OptixPipelineCompileOptions mPCO;
    std::vector<Data>& mCData;
    std::vector<Data>& mHData;
    std::set<OptixProgramGroup>& mGroups;
    std::vector<std::shared_ptr<Light>>& mLights;
    std::unordered_map<std::string, Module> mModules;
    std::unordered_map<std::string, std::shared_ptr<Asset>> mAssets;
    std::unordered_map<std::string, std::shared_ptr<Config>> mAssetConfig;

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
          mMCO(MCO), mPCO(PCO), mCData(cdata), mHData(hdata), mLights(lights),
          mGroups(group) {
        for(auto&& asset : assCfg->expand()) {
            mAssetConfig[asset->attribute("Name")->asString()] = asset;
        }
    }
    unsigned addCallable(OptixProgramGroup group, const Data& sbtData) {
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
