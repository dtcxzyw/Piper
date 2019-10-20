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

namespace fs = std::experimental::filesystem;
#define ASSERT(expr, msg) \
    if(!(expr))           \
        BUS_TRACE_THROW(std::runtime_error(msg));

using Bus::Unmoveable;
class Config;
class Asset;
class Light;
using Name = std::string_view;

class PluginHelperAPI : private Unmoveable {
private:
    virtual std::shared_ptr<Asset>
    instantiateAssetImpl(Bus::Name api, std::shared_ptr<Config> cfg) = 0;

public:
    virtual fs::path scenePath() const = 0;
    virtual OptixDeviceContext getContext() const = 0;
    virtual bool isDebug() const = 0;
    virtual OptixModule loadModuleFromPTX(const std::string& ptx) = 0;
    virtual OptixModule loadModuleFromFile(const fs::path& path) = 0;
    virtual OptixModule loadModuleFromSrc(const std::string& src) = 0;
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
