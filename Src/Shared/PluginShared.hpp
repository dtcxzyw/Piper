#pragma once
#include "Shared.hpp"
#pragma warning(push, 0)
#include "../ThirdParty/Bus/BusModule.hpp"
#include "../ThirdParty/Bus/BusReporter.hpp"
#include "../ThirdParty/Bus/BusSystem.hpp"
#include <optixu/optixpp_namespace.h>
using Bus::ReportLevel;
#pragma warning(pop)
#include <filesystem>
#include <set>

namespace fs = std::experimental::filesystem;
#define ASSERT(expr, msg) \
    if(!(expr))           \
        BUS_TRACE_THROW(std::runtime_error(msg));

struct TextureHolder final {
    optix::Buffer data;
    optix::TextureSampler sampler;
};

enum class TextureChannel { Float = 1, Float2 = 2, Float4 = 4 };

class Unmoveable {
public:
    Unmoveable() = default;
    Unmoveable(const Unmoveable&) = delete;
    Unmoveable(Unmoveable&&) = delete;
    Unmoveable& operator=(const Unmoveable&) = delete;
    Unmoveable& operator=(Unmoveable&&) = delete;
};

class BufferMapGuard final : private Unmoveable {
private:
    void* mPtr;
    optix::Buffer mBuffer;
    unsigned mLevel;

public:
    BufferMapGuard(optix::Buffer buf, RTbuffermapflag flag, unsigned level = 0U)
        : mPtr(buf->map(level, flag)), mBuffer(buf), mLevel(level) {}
    void* raw() {
        return mPtr;
    }
    template <typename T>
    T* as() {
        return reinterpret_cast<T*>(mPtr);
    }
    template <typename T>
    T& ref() {
        return *as<T>();
    }
    ~BufferMapGuard() {
        mBuffer->unmap();
    }
};

class Config;
class PluginHelperAPI {
public:
    virtual optix::Context getContext() const = 0;
    virtual fs::path scenePath() const = 0;
    virtual optix::Program compile(const std::string& entry,
                                   const std::vector<std::string>& selfLibs,
                                   const fs::path& modulePath,
                                   const std::vector<fs::path>& thirdParty = {},
                                   bool needLib = true) = 0;
    virtual TextureHolder loadTexture(TextureChannel channel,
                                      std::shared_ptr<Config> attr) = 0;
    virtual ~PluginHelperAPI() = default;
};

using PluginHelper = std::shared_ptr<PluginHelperAPI>;
