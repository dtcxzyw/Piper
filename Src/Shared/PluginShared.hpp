#pragma once
#include "Shared.hpp"
#pragma warning(push, 0)
#include "../ThirdParty/Bus/BusModule.hpp"
#include "../ThirdParty/Bus/BusReporter.hpp"
#include "../ThirdParty/Bus/BusSystem.hpp"
#include <cuda_runtime.h>
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
/*
struct TextureHolder final {
optix::Buffer data;
optix::TextureSampler sampler;
};
*/

enum class TextureChannel { Float = 1, Float2 = 2, Float4 = 4 };

class PluginHelperAPI : private Unmoveable {
public:
    virtual fs::path scenePath() const = 0;
    virtual OptixDeviceContext getContext() const = 0;
    virtual bool isDebug() const = 0;
    virtual Module compileFile(const fs::path& path) const = 0;
    virtual Module compileSource(const std::string& src) const = 0;
    virtual Module compile(const std::string& ptx) const = 0;
    virtual ~PluginHelperAPI() = default;
};

using PluginHelper = PluginHelperAPI*;
