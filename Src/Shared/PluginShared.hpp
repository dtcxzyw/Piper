#pragma once
#include "Shared.hpp"
#pragma warning(push, 0)
#include "../ThirdParty/Bus/BusModule.hpp"
#include "../ThirdParty/Bus/BusReporter.hpp"
#include "../ThirdParty/Bus/BusSystem.hpp"
#include <cuda.h>
using Bus::ReportLevel;
#pragma warning(pop)
#include "OptixHelper.hpp"
#include <filesystem>
#include <set>

namespace fs = std::filesystem;
#define ASSERT(expr, msg) \
    if(!(expr))           \
        BUS_TRACE_THROW(std::runtime_error(msg));

using Bus::Unmoveable;
class Config;
class Asset;
class Light;
using Name = std::string_view;

struct ModuleDeleter final {
    void operator()(OptixModule mod) const {
        checkOptixError(optixModuleDestroy(mod));
    }
};
using Module = std::unique_ptr<OptixModule_t, ModuleDeleter>;
struct ModuleDesc final {
    Module handle;
    std::map<std::string, std::string> nameMap;
    ModuleDesc() = default;
    const char* map(const std::string& name) const {
        auto iter = nameMap.find(name);
        if(iter != nameMap.cend())
            return iter->second.c_str();
        return nullptr;
    }
};

class ModuleManagerAPI : private Unmoveable {
public:
    virtual const ModuleDesc&
    getModule(const std::string& id,
              const std::function<std::string()>& ptxGen) = 0;
    virtual const ModuleDesc& getModuleFromFile(const fs::path& file) = 0;
    virtual std::string compileSrc(const std::string& src) = 0;
    virtual std::string compileNVVMIR(const std::vector<Data>& bitcode) = 0;
    virtual Data linkPTX(const std::vector<std::string>& ptx) = 0;
    virtual std::string cubin2PTX(const Data& cubin) = 0;
};

using ModuleManager = ModuleManagerAPI*;

class PluginHelperAPI : private Unmoveable {
private:
    virtual std::shared_ptr<Asset>
    instantiateAssetImpl(Bus::Name api, std::shared_ptr<Config> cfg) = 0;

public:
    virtual fs::path scenePath() const = 0;
    virtual OptixDeviceContext getContext() const = 0;
    virtual bool isDebug() const = 0;
    virtual ModuleManager getModuleManager() = 0;
    virtual unsigned addCallable(OptixProgramGroup group,
                                 const Data& sbtData) = 0;
    virtual unsigned addHitGroup(OptixProgramGroup radGroup, const Data& rad,
                                 OptixProgramGroup occGroup,
                                 const Data& occ) = 0;
    virtual void addLight(std::shared_ptr<Light> light) = 0;
    template <typename T>
    std::shared_ptr<T> instantiateAsset(std::shared_ptr<Config> cfg) {
        return std::dynamic_pointer_cast<T>(
            instantiateAssetImpl(T::getInterface(), cfg));
    }
    virtual ~PluginHelperAPI() = default;
};

using PluginHelper = PluginHelperAPI*;

class Asset : public Bus::ModuleFunctionBase {
protected:
    explicit Asset(Bus::ModuleInstance& instance)
        : ModuleFunctionBase(instance) {}

public:
    virtual void init(PluginHelper helper, std::shared_ptr<Config> config) = 0;
};

struct SRT final {
    Vec3 scale;
    Quat rotate;
    Vec3 trans;
    Mat4 getPointTrans() const {
        Mat4 t = glm::identity<Mat4>();
        t[0][0] = scale.x, t[1][1] = scale.y, t[2][2] = scale.z;
        t *= glm::mat4_cast(rotate);
        t[0][3] = trans.x, t[1][3] = trans.y, t[2][3] = trans.z;
        return t;
    }
};
