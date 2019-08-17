#define CORRADE_DYNAMIC_PLUGIN
#include "../DriverAPI.hpp"
#include <iostream>
#include <fstream>
#pragma warning(push,0)
#include <OpenEXR/ImfRgbaFile.h>
#pragma warning(pop)

class FixedSampler final : public Driver {
private:
    optix::Context mContext;
    fs::path mOutput;
    unsigned mSample, mWidth, mHeight;
public:
    explicit FixedSampler(PM::AbstractManager &manager,
        const std::string &plugin) : Driver{ manager, plugin } {}
    uint2 init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) override {
        mContext = helper->getContext();
        mOutput = config->toString("Output");
        mSample = config->toUint("Sample");
        mWidth = config->toUint("Width");
        mHeight = config->toUint("Height");
        return make_uint2(mWidth, mHeight);
    }
    void doRender() override {
        optix::Buffer outputBuffer = mContext->createBuffer(
            RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, mWidth, mHeight);
        {
            BufferMapGuard guard(outputBuffer, RT_BUFFER_MAP_WRITE_DISCARD);
            memset(guard.raw(), 0, sizeof(float4) * mWidth * mHeight);
        }
        outputBuffer->validate();
        mContext["driverOutputBuffer"]->set(outputBuffer);
        mContext["driverBegin"]->setUint(make_uint2(0));
        for (unsigned i = 0; i < mSample; ++i) {
            mContext["driverLaunchIndex"]->setUint(i);
            mContext->launch(0, mWidth, mHeight);
            std::cout << "Process:" << (i + 1) * 100.0 / mSample << "%" << std::endl;
        }
        {
            unsigned size = mWidth * mHeight;
            BufferMapGuard guard(outputBuffer, RT_BUFFER_MAP_READ);
            float4 *arr = guard.as<float4>();
            for (unsigned i = 0; i < size; ++i)
                if (arr[i].w > 0.0f)
                    arr[i] = arr[i] / arr[i].w;
                else arr[i] = make_float4(0.0f, 0.0f, 0.0f, 1.0f);
            try {
                Imf::RgbaOutputFile out(mOutput.string().c_str(), mWidth, mHeight,
                    Imf::WRITE_RGBA);
                out.setFrameBuffer(reinterpret_cast<Imf::Rgba *>(arr), 1, mWidth);
                out.writePixels(mHeight);
            }
            catch (const std::exception &ex) {
                FATAL << "Failed to save output file " << mOutput <<
                    " : " << ex.what();
            }
        }
    }
};
CORRADE_PLUGIN_REGISTER(FixedSampler, FixedSampler, "Piper.Driver:1")
