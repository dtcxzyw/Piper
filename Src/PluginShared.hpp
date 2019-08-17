#pragma once
#include "Common.hpp"
#pragma warning(push, 0)
#include <Corrade/PluginManager/AbstractPlugin.h>
#include <Corrade/PluginManager/AbstractManager.h>
#include <optixu/optixpp_namespace.h>
#include <nlohmann/json.hpp>
#pragma warning(pop)
#include <filesystem>
#include <set>
#include "Shared.hpp"

using Json=nlohmann::json;
namespace PM = Corrade::PluginManager;
namespace fs = std::experimental::filesystem;

class JsonHelperAPI;
using JsonHelper=std::shared_ptr<JsonHelperAPI>;

class JsonHelperAPI {
public:
    virtual std::string getString(const std::string &attr, const std::string &def) = 0;
    virtual float getFloat(const std::string &attr, float def) = 0;
    virtual Vec3 getVec3(const std::string &attr, const Vec3 &def) = 0;
    virtual Vec4 getVec4(const std::string &attr, const Vec4 &def) = 0;
    virtual Vec4 toVec4(const std::string &attr) = 0;
    virtual Vec3 toVec3(const std::string &attr) = 0;
    virtual Vec2 toVec2(const std::string &attr) = 0;
    virtual uint2 toUint2(const std::string &attr) = 0;
    virtual unsigned toUint(const std::string &attr) = 0;
    virtual float toFloat(const std::string &attr) = 0;
    virtual std::string toString(const std::string &attr) = 0;
    virtual bool toBool(const std::string &attr) = 0;
    virtual bool hasAttr(const std::string &attr) const = 0;
    virtual JsonHelper attribute(const std::string &attr) = 0;
    virtual std::vector<JsonHelper> expand() const = 0;
    virtual Json::value_t getType() const = 0;
    virtual const Json &raw() const = 0;
    virtual std::string path() const = 0;
    virtual ~JsonHelperAPI() = default;
};

JsonHelper buildJsonHelper(const Json &root);

struct TextureHolder final {
    optix::Buffer data;
    optix::TextureSampler sampler;
};

enum class TextureChannel {
    Float = 1, Float2 = 2, Float4 = 4
};

class Unmoveable {
public:
    Unmoveable() = default;
    Unmoveable(const Unmoveable &) = delete;
    Unmoveable(Unmoveable &&) = delete;
    Unmoveable &operator=(const Unmoveable &) = delete;
    Unmoveable &operator=(Unmoveable &&) = delete;
};

class BufferMapGuard final :private Unmoveable {
private:
    void *mPtr;
    optix::Buffer mBuffer;
    unsigned mLevel;
public:
    BufferMapGuard(optix::Buffer buf, RTbuffermapflag flag,
        unsigned level = 0U) :mPtr(buf->map(level, flag)), mBuffer(buf),
        mLevel(level) {}
    void *raw() {
        return mPtr;
    }
    template<typename T>
    T *as() {
        return reinterpret_cast<T *>(mPtr);
    }
    template<typename T>
    T &ref() {
        return *as<T>();
    }
    ~BufferMapGuard() {
        mBuffer->unmap();
    }
};

class PluginHelperAPI {
public:
    virtual optix::Context getContext() const = 0;
    virtual fs::path scenePath() const = 0;
    virtual optix::Program compile(const std::string &entry,
        const std::vector<std::string> &selfLibs, const fs::path &modulePath,
        const std::vector<fs::path> &thirdParty = {}, bool needLib = true) = 0;
    virtual TextureHolder loadTexture(TextureChannel channel, JsonHelper attr) = 0;
    virtual ~PluginHelperAPI() = default;
};

using PluginHelper=std::shared_ptr<PluginHelperAPI>;

PluginHelper buildPluginHelper(optix::Context context, const fs::path &runtimeLib,
    const fs::path &scenePath);

class PluginSharedAPI :public PM::AbstractPlugin {
public:
    explicit PluginSharedAPI(PM::AbstractManager &manager,
        const std::string &plugin)
        : AbstractPlugin{ manager, plugin } {}
    virtual void command(int argc, char **argv) {};
};
