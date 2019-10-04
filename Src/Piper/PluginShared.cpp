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

public:
    PluginHelperImpl(OptixDeviceContext context, const fs::path& scenePath,
                     bool debug, const OptixModuleCompileOptions& MCO,
                     const OptixPipelineCompileOptions& PCO)
        : mContext(context), mScenePath(scenePath), mDebug(debug), mMCO(MCO),
          mPCO(PCO) {}
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
    Module compileSource(const std::string& src) const override;
};

std::unique_ptr<PluginHelperAPI>
buildPluginHelper(OptixDeviceContext context, const fs::path& scenePath,
                  bool debug, const OptixModuleCompileOptions& MCO,
                  const OptixPipelineCompileOptions& PCO) {
    return std::make_unique<PluginHelperImpl>(context, scenePath, debug, MCO,
                                              PCO);
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

Module PluginHelperImpl::compileSource(const std::string& src) const {
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
        return compile(ptx);
    }
    BUS_TRACE_END();
}

/*
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
*/
