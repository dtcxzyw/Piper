#include "../../Shared/DriverAPI.hpp"
#include <fstream>
#include <iostream>
#pragma warning(push, 0)
#define OPENEXR_DLL
#include <OpenEXR/ImfRgbaFile.h>
#pragma warning(pop)

const char* moduleName = "Piper.BuiltinDriver.FixedSampler";

class FixedSampler final : public Driver {
private:
    optix::Context mContext;
    fs::path mOutput;
    unsigned mSample, mWidth, mHeight;
    bool mFiltBadColor;

public:
    explicit FixedSampler(Bus::ModuleInstance& instance) : Driver(instance) {}
    uint2 init(PluginHelper helper, std::shared_ptr<Config> config) override {
        mContext = helper->getContext();
        mOutput = config->attribute("Output")->asString();
        mSample = config->attribute("Sample")->asUint();
        mWidth = config->attribute("Width")->asUint();
        mHeight = config->attribute("Height")->asUint();
        mFiltBadColor = config->getBool("FiltBadColor", false);
        return make_uint2(mWidth, mHeight);
    }
    void doRender() override {
        BUS_TRACE_BEGIN(moduleName) {
            optix::Buffer outputBuffer = mContext->createBuffer(
                RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, mWidth, mHeight);
            {
                BufferMapGuard guard(outputBuffer, RT_BUFFER_MAP_WRITE_DISCARD);
                memset(guard.raw(), 0, sizeof(float4) * mWidth * mHeight);
            }
            outputBuffer->validate();
            mContext["driverOutputBuffer"]->set(outputBuffer);
            mContext["driverBegin"]->setUint(make_uint2(0));
            mContext["driverFiltBadColor"]->setInt(mFiltBadColor);
            for(unsigned i = 0; i < mSample; ++i) {
                mContext["driverLaunchIndex"]->setUint(i);
                mContext->validate();
                mContext->launch(0, mWidth, mHeight);
                std::cout.precision(2);
                std::cout << std::fixed
                          << "Process:" << (i + 1) * 100.0 / mSample << "%"
                          << std::endl;
            }
            {
                unsigned size = mWidth * mHeight;
                BufferMapGuard guard(outputBuffer, RT_BUFFER_MAP_READ);
                float4* arr = guard.as<float4>();
                std::vector<Imf::Rgba> rgba(mWidth * mHeight);
                bool badColor = false;
                for(unsigned i = 0; i < size; ++i) {
                    if(isfinite(arr[i].x) && isfinite(arr[i].y) &&
                       isfinite(arr[i].z) && isfinite(arr[i].w)) {
                        if(arr[i].w > 0.0f)
                            rgba[i] = Imf::Rgba(arr[i].x / arr[i].w,
                                                arr[i].y / arr[i].w,
                                                arr[i].z / arr[i].w);
                        else
                            rgba[i] = Imf::Rgba(0.0f, 0.0f, 0.0f);
                    } else
                        rgba[i] = Imf::Rgba(1.0f, 0.0f, 1.0f), badColor = true;
                }
                if(badColor)
                    reporter().apply(
                        ReportLevel::Warning, "Bad color!!!",
                        BUS_SRCLOC("Piper.BuiltinDriver.FixedSampler"));
                try {
                    Imf::RgbaOutputFile out(mOutput.string().c_str(), mWidth,
                                            mHeight, Imf::WRITE_RGB);
                    out.setFrameBuffer(rgba.data(), 1, mWidth);
                    out.writePixels(mHeight);
                } catch(...) {
                    std::throw_with_nested(std::runtime_error(
                        "Failed to save output file " + mOutput.string()));
                }
            }
        }
        BUS_TRACE_END();
    }
};

class Instance final : public Bus::ModuleInstance {
public:
    Instance(const fs::path& path, Bus::ModuleSystem& sys)
        : Bus::ModuleInstance(path, sys) {}
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = moduleName;
        res.guid = Bus::str2GUID("{1ED1289A-88C7-4426-848D-26166A602F59}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "FixedSampler";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Driver::getInterface())
            return { "FixedSampler" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "FixedSampler")
            return std::make_shared<FixedSampler>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
