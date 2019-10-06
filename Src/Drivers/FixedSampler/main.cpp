#include "../../Shared/DriverAPI.hpp"
#include "DataDesc.hpp"
#include <fstream>
#include <iostream>
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#define OPENEXR_DLL
#include <OpenEXR/ImfRgbaFile.h>
#pragma warning(pop)

BUS_MODULE_NAME("Piper.BuiltinDriver.FixedSampler");

class FixedSampler final : public Driver {
private:
    fs::path mOutput;
    unsigned mSample, mWidth, mHeight;
    bool mFiltBadColor;
    Module mModule;
    ProgramGroup mMissRad, mMissOcc, mRayGen, mException;

public:
    explicit FixedSampler(Bus::ModuleInstance& instance) : Driver(instance) {}
    DriverData init(PluginHelper helper,
                    std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            mOutput = config->attribute("Output")->asString();
            mSample = config->attribute("Sample")->asUint();
            mWidth = config->attribute("Width")->asUint();
            mHeight = config->attribute("Height")->asUint();
            mFiltBadColor = config->getBool("FiltBadColor", false);
            mModule =
                helper->compileFile(modulePath().parent_path() / "Kernel.ptx");
            OptixProgramGroupDesc desc[4] = {};
            desc[0].flags = 0;
            desc[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
            desc[0].raygen.entryFunctionName = "__raygen__renderKernel";
            desc[0].raygen.module = mModule.get();
            desc[1].flags = 0;
            desc[1].kind = OPTIX_PROGRAM_GROUP_KIND_EXCEPTION;
            desc[1].exception.entryFunctionName = "__exception__default";
            desc[1].exception.module = mModule.get();
            desc[2].flags = 0;
            desc[2].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
            desc[2].miss.entryFunctionName = "__miss__rad";
            desc[2].miss.module = mModule.get();
            desc[3].flags = 0;
            desc[3].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
            desc[3].miss.entryFunctionName = "__miss__occ";
            desc[3].miss.module = mModule.get();
            OptixProgramGroup groups[4];
            OptixProgramGroupOptions opt = {};
            checkOptixError(optixProgramGroupCreate(
                helper->getContext(), desc, 4, &opt, nullptr, nullptr, groups));
            mRayGen.reset(groups[0]);
            mException.reset(groups[1]);
            mMissRad.reset(groups[2]);
            mMissOcc.reset(groups[3]);
            DriverData res;
            res.group.assign(groups, groups + 4);
            return res;
        }
        BUS_TRACE_END();
    }
    void doRender(DriverHelper helper) override {
        BUS_TRACE_BEG() {
            DataDesc data;
            data.filtBadColor = mFiltBadColor;
            data.height = mHeight;
            data.width = mWidth;
            Buffer output = allocBuffer(sizeof(Vec4) * mWidth * mHeight, 16);
            checkCudaError(cuMemsetD16(asPtr(output), 0,
                                       sizeof(Vec4) / 16 * mWidth * mHeight));
            data.outputBuffer = static_cast<Vec4*>(output.get());
            for(unsigned i = 0; i < mSample; ++i) {
                data.sampleIdx = i;
                Buffer rayGenSBT = uploadData(helper->getStream(),
                                              packSBT(mRayGen.get(), data));
                Buffer exceptionSBT = uploadData(
                    helper->getStream(), packEmptySBT(mException.get()));
                Data missRadSBT = packEmptySBT(mMissRad.get());
                Data missOccSBT = packEmptySBT(mMissOcc.get());
                std::vector<Data> missSBT;
                missSBT.emplace_back(missRadSBT);
                missSBT.emplace_back(missOccSBT);
                unsigned count, stride;
                CUdeviceptr ptr;
                Buffer buf = uploadSBTRecords(helper->getStream(), missSBT, ptr,
                                              stride, count);
                helper->doRender(
                    { mWidth, mHeight }, [&](OptixShaderBindingTable& table) {
                        table.exceptionRecord = asPtr(exceptionSBT);
                        table.raygenRecord = asPtr(rayGenSBT);
                        table.missRecordCount = count;
                        table.missRecordBase = ptr;
                        table.missRecordStrideInBytes = stride;
                    });
                std::stringstream ss;
                ss.precision(2);
                ss << std::fixed << "Process:" << (i + 1) * 100.0 / mSample
                   << "%" << std::endl;
                reporter().apply(ReportLevel::Info, ss.str(), BUS_DEFSRCLOC());
            }
            {
                checkCudaError(cuStreamSynchronize(helper->getStream()));
                std::vector<Vec4> arr =
                    downloadData<Vec4>(output, 0, mWidth * mHeight);
                unsigned size = mWidth * mHeight;
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
        : Bus::ModuleInstance(path, sys) {
        checkOptixError(optixInit());
    }
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = BUS_DEFAULT_MODULE_NAME;
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
