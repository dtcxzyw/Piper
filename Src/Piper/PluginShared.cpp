#include "../Shared/PluginShared.hpp"
#include "../Shared/ConfigAPI.hpp"
#include <sstream>

class PluginHelperImpl final : public PluginHelperAPI {
private:
    optix::Context mContext;
    fs::path mRuntimeLib, mScenePath;
    // std::map<size_t, optix::Program> mCachedPrograms;
public:
    PluginHelperImpl(optix::Context context, const fs::path& runtimeLib,
                     const fs::path& scenePath)
        : mContext(context), mRuntimeLib(runtimeLib), mScenePath(scenePath) {}
    optix::Context getContext() const override {
        return mContext;
    }
    fs::path scenePath() const override {
        return mScenePath;
    }
    optix::Program compile(const std::string& entry,
                           const std::vector<std::string>& selfLibs,
                           const fs::path& modulePath,
                           const std::vector<fs::path>& thirdParty,
                           bool needLib) {
        /*
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
        */
        BUS_TRACE_BEGIN("Piper.Builtin.PluginHelper") {
            std::vector<std::string> files;
            if(needLib)
                files.emplace_back(mRuntimeLib.string());
            for(const auto& third : thirdParty)
                files.emplace_back(third.string());
            for(const auto& lib : selfLibs)
                files.emplace_back((modulePath / lib).string());
            try {
                optix::Program res =
                    mContext->createProgramFromPTXFiles(files, entry);
                res->validate();
                // return mCachedPrograms[hashValue] = res;
                return res;
            } catch(...) {
                std::stringstream ss;
                ss << "Complie error [entry=" << entry << ", file=";
                for(auto f : files)
                    ss << f << ",";
                ss << "]";
                std::throw_with_nested(std::runtime_error(ss.str()));
            }
        }
        BUS_TRACE_END();
    }
    TextureHolder loadTexture(TextureChannel channel,
                              std::shared_ptr<Config> attr) override;
};

PluginHelper buildPluginHelper(optix::Context context,
                               const fs::path& runtimeLib,
                               const fs::path& scenePath) {
    return std::make_shared<PluginHelperImpl>(context, runtimeLib, scenePath);
}

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

TextureHolder PluginHelperImpl::loadTexture(TextureChannel channel,
                                            std::shared_ptr<Config> attr) {
    BUS_TRACE_BEGIN("Piper.Builtin.PluginHelper") {
        optix::Buffer buffer = mContext->createBuffer(RT_BUFFER_INPUT);
        RTformat format;
        switch(channel) {
            case TextureChannel::Float:
                format = RT_FORMAT_FLOAT;
                break;
            case TextureChannel::Float2:
                format = RT_FORMAT_FLOAT2;
                break;
            case TextureChannel::Float4:
                format = RT_FORMAT_FLOAT4;
                break;
            default:
                BUS_TRACE_THROW(std::runtime_error("Unknown channel."));
                break;
        }
        switch(attr->getType()) {
            case DataType::Object: {
                fs::path imageFile =
                    mScenePath / attr->attribute("File")->asString();
                struct ImageDeleter final {
                    void operator()(void* ptr) const {
                        stbi_image_free(ptr);
                    }
                };
                using Holder = std::unique_ptr<float, ImageDeleter>;
                std::string file = imageFile.string();
                int w, h, comp;
                stbi_ldr_to_hdr_gamma(1.0f);
                auto res = Holder(stbi_loadf(file.c_str(), &w, &h, &comp,
                                             static_cast<int>(channel)));
                ASSERT(res,
                       std::string("Failed to load image:") +
                           stbi_failure_reason());
                ASSERT(comp == static_cast<int>(channel),
                       "Need " + std::to_string(static_cast<int>(channel)) +
                           " channel but read " + std::to_string(comp) +
                           "(path=" + attr->path() + ",file=" + file + ")");
                buffer->setSize(w, h);
                buffer->setFormat(format);
                {
                    BufferMapGuard guard(buffer, RT_BUFFER_MAP_WRITE_DISCARD);
                    memcpy(guard.raw(), res.get(),
                           sizeof(float) * static_cast<int>(channel) * w * h);
                }
            } break;
            case DataType::Float: {
                float val = attr->asFloat();
                buffer->setSize(1, 1);
                buffer->setFormat(format);
                {
                    BufferMapGuard guard(buffer, RT_BUFFER_MAP_WRITE_DISCARD);
                    float4 data = make_float4(val);
                    memcpy(guard.raw(), &data,
                           static_cast<int>(channel) * sizeof(float));
                }
            } break;
            case DataType::Array: {
                auto elements = attr->expand();
                bool fill =
                    elements.size() == 3 && channel == TextureChannel::Float4;
                ASSERT(static_cast<size_t>(channel) == elements.size() || fill,
                       "Need " + std::to_string(static_cast<int>(channel)) +
                           " channel but read " +
                           std::to_string(elements.size()) +
                           "(path=" + attr->path() + ")");
                float data[4];
                for(int i = 0; i < elements.size(); ++i)
                    data[i] = elements[i]->asFloat();
                buffer->setSize(1, 1);
                buffer->setFormat(format);
                {
                    BufferMapGuard guard(buffer, RT_BUFFER_MAP_WRITE_DISCARD);
                    memcpy(guard.raw(), data,
                           static_cast<int>(channel) * sizeof(float));
                }
            } break;
            default: {
                BUS_TRACE_THROW(std::runtime_error(
                    "Failed to load texture.(path=" + attr->path() + ")"));
            } break;
        }
        buffer->validate();
        optix::TextureSampler sampler = mContext->createTextureSampler();
        if(attr->getType() == DataType::Object) {
            {
                auto read = [&](const std::string& str,
                                const std::string& def) {
                    auto filter = attr->getString(str, def);
                    if(filter == "Linear")
                        return RT_FILTER_LINEAR;
                    else if(filter == "Nearest")
                        return RT_FILTER_NEAREST;
                    else if(filter == "None")
                        return RT_FILTER_NONE;
                    else {
                        BUS_TRACE_THROW(
                            std::runtime_error("Unknown filter " + filter +
                                               "(path=" + attr->path() + ")."));
                    }
                };
                sampler->setFilteringModes(read("MinFilter", "Linear"),
                                           read("MagFilter", "Linear"),
                                           read("MipMapFilter", "None"));
            }
            {
                auto read = [&](const std::string& str,
                                const std::string& def) {
                    auto mode = attr->getString(str, def);
                    if(mode == "Repeat")
                        return RT_WRAP_REPEAT;
                    else if(mode == "Mirror")
                        return RT_WRAP_MIRROR;
                    else if(mode == "ClampToBorder")
                        return RT_WRAP_CLAMP_TO_BORDER;
                    else if(mode == "ClampToEdge")
                        return RT_WRAP_CLAMP_TO_EDGE;
                    else {
                        BUS_TRACE_THROW(
                            std::runtime_error("Unknown mode " + mode +
                                               "(path=" + attr->path() + ")."));
                    }
                };
                sampler->setWrapMode(0, read("WarpS", "Repeat"));
                sampler->setWrapMode(1, read("WarpT", "Repeat"));
            }
            sampler->setMaxAnisotropy(attr->getFloat("MaxAnisotropy", 1.0f));
        } else {
            sampler->setFilteringModes(RT_FILTER_NEAREST, RT_FILTER_NEAREST,
                                       RT_FILTER_NONE);
            sampler->setWrapMode(0, RT_WRAP_CLAMP_TO_EDGE);
            sampler->setWrapMode(1, RT_WRAP_CLAMP_TO_EDGE);
        }
        sampler->setIndexingMode(RT_TEXTURE_INDEX_NORMALIZED_COORDINATES);
        sampler->setReadMode(RT_TEXTURE_READ_ELEMENT_TYPE);
        sampler->setBuffer(buffer);
        sampler->validate();
        TextureHolder res;
        res.data = buffer;
        res.sampler = sampler;
        return res;
    }
    BUS_TRACE_END();
}
