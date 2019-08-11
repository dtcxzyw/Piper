#include "../PluginShared.hpp"

#define ASSERTCONFIG(expr) ASSERT(expr,msg+"(path="+mPath+")")

class JsonHelperImpl final :public JsonHelperAPI {
private:
    std::set<std::string> mAttrs;
    const Json &mRef;
    std::string mPath;
    const Json &access(const std::string &attr) {
        std::string msg = "Need Object";
        ASSERTCONFIG(mRef.is_object());
        msg = "Need attribute " + attr;
        ASSERTCONFIG(mRef.contains(attr));
        mAttrs.erase(attr);
        return mRef[attr];
    }
public:
    JsonHelperImpl(const Json &json, const std::string &path) :mRef(json), mPath(path) {
        if (json.is_object()) {
            for (auto iter = json.cbegin(); iter != json.cend(); ++iter) {
                std::string key = iter.key();
                if (key.find("[Comment]") != 0)
                    mAttrs.insert(key);
            }
        }
    }
    Vec3 toVec3(const std::string &attr) override {
        const Json &config = access(attr);
        static const  std::string msg = "Need Vec3.";
        ASSERTCONFIG(config.is_array() && config.size() == 3);
        Vec3 res;
        auto beg = config.cbegin();
        ASSERTCONFIG(beg->is_number_float());
        res.x = static_cast<float>(beg->get<double>());
        ++beg;
        ASSERTCONFIG(beg->is_number_float());
        res.y = static_cast<float>(beg->get<double>());
        ++beg;
        ASSERTCONFIG(beg->is_number_float());
        res.z = static_cast<float>(beg->get<double>());
        return res;
    }
    Vec2 toVec2(const std::string &attr) override {
        const Json &config = access(attr);
        static const  std::string msg = "Need Vec2.";
        ASSERTCONFIG(config.is_array() && config.size() == 2);
        Vec2 res;
        auto beg = config.cbegin();
        ASSERTCONFIG(beg->is_number_float());
        res.x = static_cast<float>(beg->get<double>());
        ++beg;
        ASSERTCONFIG(beg->is_number_float());
        res.y = static_cast<float>(beg->get<double>());
        return res;
    }
    uint2 toUint2(const std::string &attr) override {
        const Json &config = access(attr);
        static const  std::string msg = "Need Uint2.";
        ASSERTCONFIG(config.is_array() && config.size() == 2);
        uint2 res;
        auto beg = config.cbegin();
        ASSERTCONFIG(beg->is_number_unsigned());
        res.x = static_cast<unsigned>(beg->get<uint64_t>());
        ++beg;
        ASSERTCONFIG(beg->is_number_unsigned());
        res.y = static_cast<unsigned>(beg->get<uint64_t>());
        return res;
    }
    float toFloat(const std::string &attr) override {
        const Json &json = access(attr);
        static const  std::string msg = "Need Float.";
        ASSERTCONFIG(json.is_number_float());
        return static_cast<float>(json.get<double>());
    }
    std::string toString(const std::string &attr) override {
        const Json &json = access(attr);
        static const std::string msg = "Need string.";
        ASSERTCONFIG(json.is_string());
        return json.get<std::string>();
    }
    bool toBool(const std::string &attr) override {
        const Json &config = access(attr);
        static const std::string msg = "Need Boolean.";
        ASSERTCONFIG(config.is_boolean());
        return config.get<bool>();
    }
    bool hasAttr(const std::string &attr) const override {
        static const std::string msg = "Need Object.";
        ASSERTCONFIG(mRef.is_object());
        return mRef.contains(attr);
    }
    JsonHelper attribute(const std::string &attr) override {
        return std::make_shared<JsonHelperImpl>(access(attr), mPath + "/" + attr);
    }
    std::vector<JsonHelper> expand() const override {
        static const std::string msg = "Need Array.";
        ASSERTCONFIG(mRef.is_array());
        std::vector<JsonHelper> res;
        uint32_t index = 0;
        for (const auto &ele : mRef)
            res.push_back(std::make_shared<JsonHelperImpl>(ele,
            mPath + "/[" + std::to_string(index) + "]"));
        return res;
    }
    Json::value_t getType() const override {
        return mRef.type();
    }
    ~JsonHelperImpl() {
        if (mRef.is_object() && !mAttrs.empty()) {
            std::string ss = "Unvisited attributes(path=" + mPath + "):";
            for (auto attr : mAttrs)
                ss += attr + ",";
            ss.pop_back();
            WARNING << ss;
        }
    }
    const Json &raw() const override {
        return mRef;
    }
    std::string path() const override {
        return mPath;
    }
    std::string getString(const std::string &attr, const std::string &def) override {
        if (hasAttr(attr))
            return toString(attr);
        return def;
    }
    float getFloat(const std::string &attr, float def) override {
        if (hasAttr(attr))
            return toFloat(attr);
        return def;
    }
    unsigned toUint(const std::string &attr) override {
        const Json &config = access(attr);
        static const std::string msg = "Need unsigned integer.";
        ASSERTCONFIG(config.is_number_unsigned());
        return config.get<unsigned>();
    }
};

JsonHelper buildJsonHelper(const Json &root) {
    return std::make_shared<JsonHelperImpl>(root, "root");
}

class PluginHelperImpl final :public PluginHelperAPI {
private:
    optix::Context mContext;
    fs::path mRuntimeLib, mScenePath;
    std::map<size_t, optix::Program> mCachedPrograms;
public:
    PluginHelperImpl(optix::Context context, const fs::path &runtimeLib,
        const fs::path &scenePath)
        :mContext(context), mRuntimeLib(runtimeLib), mScenePath(scenePath) {}
    optix::Context getContext() const override {
        return mContext;
    }
    fs::path scenePath() const override {
        return mScenePath;
    }
    optix::Program compile(const std::string &entry,
        const std::vector<std::string> &selfLibs, const fs::path &modulePath,
        const std::vector<fs::path> &thirdParty) {
        std::hash<std::string> hasher;
        size_t hashValue = hasher("RTL@" + mRuntimeLib.string()) ^
            hasher("ENTRY@" + entry) ^
            hasher("BASE@" + modulePath.string());
        for (const auto &lib : selfLibs)
            hashValue ^= hasher("SELF@" + lib);
        for (const auto &third : thirdParty)
            hashValue ^= hasher("THIRD@" + third.string());
        auto iter = mCachedPrograms.find(hashValue);
        if (iter != mCachedPrograms.cend())
            return iter->second;
        std::vector<std::string> files{ mRuntimeLib.string() };
        for (const auto &third : thirdParty)
            files.emplace_back(third.string());
        for (const auto &lib : selfLibs)
            files.emplace_back((modulePath / lib).string());
        optix::Program res = mContext->createProgramFromPTXFiles(files, entry);
        return mCachedPrograms[hashValue] = res;
    }
    TextureHolder loadTexture(TextureChannel channel, JsonHelper attr) override;
};

PluginHelper buildPluginHelper(optix::Context context, const fs::path &runtimeLib,
    const fs::path &scenePath) {
    return std::make_shared<PluginHelperImpl>(context, runtimeLib, scenePath);
}

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

TextureHolder PluginHelperImpl::loadTexture(TextureChannel channel,
    JsonHelper attr) {
    using Type=Json::value_t;
    optix::Buffer buffer = mContext->createBuffer(RT_BUFFER_INPUT);
    RTformat format;
    switch (channel) {
        case TextureChannel::Float:format = RT_FORMAT_FLOAT;
            break;
        case TextureChannel::Float2:format = RT_FORMAT_FLOAT2;
            break;
        case TextureChannel::Float4:format = RT_FORMAT_FLOAT4;
            break;
        default:FATAL << "Unknown channel.";
            break;
    }
    switch (attr->getType()) {
        case Type::object:
        {
            fs::path imageFile = mScenePath / attr->toString("File");
            struct ImageDeleter final {
                void operator()(void *ptr) const {
                    stbi_image_free(ptr);
                }
            };
            using Holder = std::unique_ptr<float, ImageDeleter>;
            std::string file = imageFile.string();
            int w, h, comp;
            stbi_ldr_to_hdr_gamma(1.0f);
            auto res = Holder(stbi_loadf(file.c_str(), &w, &h, &comp,
                static_cast<int>(channel)));
            if (!res)
                FATAL << "Failed to load image:" << stbi_failure_reason();
            if (comp != static_cast<int>(channel))
                FATAL << "Need " << static_cast<int>(channel) << " channel but read "
                << comp << "(path=" << attr->path() << ",file=" << file << ")";
            buffer->setSize(w, h);
            buffer->setFormat(format);
            {
                BufferMapGuard guard(buffer, RT_BUFFER_MAP_WRITE_DISCARD);
                memcpy(guard.raw(), res.get(), sizeof(float) *
                    static_cast<int>(channel) * w * h);
            }
        } break;
        case Type::number_float:
        {
            float val = static_cast<float>(attr->raw().get<double>());
            buffer->setSize(1, 1);
            buffer->setFormat(format);
            {
                BufferMapGuard guard(buffer, RT_BUFFER_MAP_WRITE_DISCARD);
                float4 data = make_float4(val);
                memcpy(guard.raw(), &data, static_cast<int>(channel) * sizeof(float));
            }
        }break;
        case Type::array:
        {
            auto elements = attr->expand();
            if (static_cast<size_t>(channel) != elements.size())
                FATAL << "Need " << static_cast<int>(channel) << " channel but read "
                << elements.size() << "(path=" << attr->path() << ")";
            float data[4];
            for (int i = 0; i < static_cast<int>(channel); ++i) {
                JsonHelper ele = elements[i];
                ASSERT(ele->getType() == Type::number_float, "Need float.(path="
                    << attr->path() << ")");
                data[i] = static_cast<float>(ele->raw().get<double>());
            }
            buffer->setSize(1, 1);
            buffer->setFormat(format);
            {
                BufferMapGuard guard(buffer, RT_BUFFER_MAP_WRITE_DISCARD);
                memcpy(guard.raw(), data, static_cast<int>(channel) * sizeof(float));
            }
        }break;
        default:
        {
            FATAL << "Failed to load texture.(path=" << attr->path() << ")";
        }
        break;
    }
    optix::TextureSampler sampler = mContext->createTextureSampler();
    {
        auto read = [&] (const std::string &str, const std::string &def) {
            auto filter = attr->getString(str, def);
            if (filter == "Linear")
                return RT_FILTER_LINEAR;
            else if (filter == "Nearest")
                return RT_FILTER_NEAREST;
            else if (filter == "None")
                return RT_FILTER_NONE;
            else FATAL << "Unknown filter " << filter << "(path="
                << attr->path() << ").";
        };
        sampler->setFilteringModes(read("MinFilter", "Linear"),
            read("MagFilter", "Linear"),
            read("MipMapFilter", "None"));
    }
    sampler->setIndexingMode(RT_TEXTURE_INDEX_NORMALIZED_COORDINATES);
    {
        auto read = [&] (const std::string &str, const std::string &def) {
            auto mode = attr->getString(str, def);
            if (mode == "Repeat")
                return RT_WRAP_REPEAT;
            else if (mode == "Mirror")
                return RT_WRAP_MIRROR;
            else if (mode == "ClampToBorder")
                return RT_WRAP_CLAMP_TO_BORDER;
            else if (mode == "ClampToEdge")
                return RT_WRAP_CLAMP_TO_EDGE;
            else FATAL << "Unknown warp mode " << mode << "(path="
                << attr->path() << ").";
        };
        sampler->setWrapMode(0, read("WarpS", "Repeat"));
        sampler->setWrapMode(1, read("WarpT", "Repeat"));
    }
    sampler->setReadMode(RT_TEXTURE_READ_ELEMENT_TYPE);
    sampler->setMaxAnisotropy(attr->getFloat("MaxAnisotropy", 1.0f));
    TextureHolder res;
    res.data = buffer;
    res.sampler = sampler;
    return res;
}
