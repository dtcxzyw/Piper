#include "../../Shared/ConfigAPI.hpp"
#include "../../Shared/DriverAPI.hpp"
#include "../../Shared/IntegratorAPI.hpp"
#include "../../Shared/LightAPI.hpp"
#include "../../Shared/PhotographerAPI.hpp"
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
    unsigned mSampleCount, mSamplePerLaunch, mGenerateRay, mSampleOnePixel;
    Uint2 mFilmSize;
    bool mFiltBadColor;
    ProgramGroup mMissOcc, mRayGen, mException;
    std::shared_ptr<Photographer> mPhotographer;
    std::shared_ptr<Integrator> mIntegrator;
    std::shared_ptr<EnvironmentLight> mEnv;
    Data mEnvData;

public:
    explicit FixedSampler(Bus::ModuleInstance& instance) : Driver(instance) {}
    DriverData init(PluginHelper helper,
                    std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            mOutput = config->attribute("Output")->asString();
            mSampleCount = config->attribute("SampleCount")->asUint();
            mSamplePerLaunch = config->attribute("SamplePerLaunch")->asUint();
            mFilmSize = config->attribute("FilmSize")->asUint2();
            mFiltBadColor = config->getBool("FiltBadColor", false);
            const ModuleDesc& mod =
                helper->getModuleManager()->getModuleFromFile(
                    modulePath().parent_path() / "Kernel.ptx");
            OptixProgramGroupDesc desc[3] = {};
            desc[0].flags = 0;
            desc[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
            desc[0].raygen.entryFunctionName =
                mod.map("__raygen__renderKernel");
            desc[0].raygen.module = mod.handle.get();
            desc[1].flags = 0;
            desc[1].kind = OPTIX_PROGRAM_GROUP_KIND_EXCEPTION;
            desc[1].exception.entryFunctionName =
                mod.map(helper->isDebug() ? "__exception__default" :
                                            "__exception__silence");
            desc[1].exception.module = mod.handle.get();
            desc[2].flags = 0;
            desc[2].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
            desc[2].miss.entryFunctionName = mod.map("__miss__occ");
            desc[2].miss.module = mod.handle.get();
            OptixProgramGroup groups[3] = {};
            OptixProgramGroupOptions opt = {};
            checkOptixError(optixProgramGroupCreate(
                helper->getContext(), desc, 3, &opt, nullptr, nullptr, groups));
            mRayGen.reset(groups[0]);
            mException.reset(groups[1]);
            mMissOcc.reset(groups[2]);

            DriverData res;
            OptixStackSizes size;
            checkOptixError(
                optixProgramGroupGetStackSize(mRayGen.get(), &size));
            res.cssRG = size.cssRG;
            checkOptixError(
                optixProgramGroupGetStackSize(mMissOcc.get(), &size));
            res.cssMSOcc = size.cssMS;
            auto pgc = config->attribute("Photographer");
            mPhotographer = system().instantiateByName<Photographer>(
                pgc->attribute("Plugin")->asString());

            CameraData cdata = mPhotographer->init(helper, pgc);
            res.dss = cdata.dss;
            mGenerateRay = helper->addCallable(
                cdata.group, mPhotographer->prepareFrame(mFilmSize));

            auto igc = config->attribute("Integrator");
            mIntegrator = system().instantiateByName<Integrator>(
                igc->attribute("Plugin")->asString());
            IntegratorData idata = mIntegrator->init(helper, igc);

            auto egc = config->attribute("Environment");
            mEnv = system().instantiateByName<EnvironmentLight>(
                egc->attribute("Plugin")->asString());
            LightData edata = mEnv->init(helper, egc);
            mEnvData = edata.sbtData;

            res.cssMSRad = edata.css;
            res.cssRG += idata.css;
            res.dss = std::max(edata.dss, std::max(res.dss, idata.dss));
            mSampleOnePixel = helper->addCallable(idata.group, idata.sbtData);
            res.maxTraceDepth = idata.maxTraceDepth;
            // TODO:exact call graph like stack size
            res.maxSampleDim =
                cdata.maxSampleDim + idata.maxSampleDim + edata.maxSampleDim;
            res.group.assign(groups, groups + 3);
            res.group.push_back(edata.group);
            res.size = mFilmSize;
            res.maxSPP = mSampleCount;
            // TODO:own Sampler
            return res;
        }
        BUS_TRACE_END();
    }
    void setStack(OptixPipeline pipeline, const StackSizeInfo& stack) override {
        BUS_TRACE_BEG() {
            // TODO:other algorithms
            unsigned css = stack.cssRG +
                std::max(stack.maxCssGeoRad + stack.maxCssLight +
                             std::max(stack.maxCssGeoOcc, stack.cssMSOcc),
                         stack.cssMSRad);
            reporter().apply(ReportLevel::Info, "css = " + std::to_string(css),
                             BUS_DEFSRCLOC());
            checkOptixError(optixPipelineSetStackSize(pipeline, stack.maxDssT,
                                                      stack.maxDssS, css,
                                                      stack.graphHeight));
        }
        BUS_TRACE_END();
    }
    void doRender(unsigned realSPP, DriverHelper helper) override {
        BUS_TRACE_BEG() {
            DataDesc data;
            data.filtBadColor = mFiltBadColor;
            data.width = mFilmSize.x;
            data.height = mFilmSize.y;
            data.sampleOnePixel = mSampleOnePixel;
            data.generateRay = mGenerateRay;
            size_t filmSize = mFilmSize.x * mFilmSize.y;
            Buffer output = allocBuffer(sizeof(Vec4) * filmSize, 16);
            checkCudaError(
                cuMemsetD16(asPtr(output), 0, sizeof(Vec4) / 16 * filmSize));
            data.outputBuffer = static_cast<Vec4*>(output.get());

            Buffer exceptionSBT = uploadData(
                helper->getStream(), packEmptySBTRecord(mException.get()));
            Data missOccSBT = packEmptySBTRecord(mMissOcc.get());
            std::vector<Data> missSBT;
            missSBT.emplace_back(mEnvData);
            missSBT.emplace_back(missOccSBT);
            unsigned count, stride;
            CUdeviceptr ptr;
            Buffer buf = uploadSBTRecords(helper->getStream(), missSBT, ptr,
                                          stride, count);

            for(unsigned beg = 0; beg < realSPP; beg += mSamplePerLaunch) {
                data.sampleIdxBeg = beg;
                data.sampleIdxEnd = std::min(beg + mSamplePerLaunch, realSPP);
                Buffer rayGenSBT = uploadData(
                    helper->getStream(), packSBTRecord(mRayGen.get(), data));

                helper->doRender([&](OptixShaderBindingTable& table) {
                    table.exceptionRecord = asPtr(exceptionSBT);
                    table.raygenRecord = asPtr(rayGenSBT);
                    table.missRecordCount = count;
                    table.missRecordBase = ptr;
                    table.missRecordStrideInBytes = stride;
                });
                {
                    std::stringstream ss;
                    ss.precision(2);
                    ss << std::fixed
                       << "Process:" << data.sampleIdxEnd * 100.0 / realSPP
                       << "%" << std::endl;
                    reporter().apply(ReportLevel::Info, ss.str(),
                                     BUS_DEFSRCLOC());
                }
            }
            {
                checkCudaError(cuStreamSynchronize(helper->getStream()));
                std::vector<Vec4> arr = downloadData<Vec4>(output, 0, filmSize);
                std::vector<Imf::Rgba> rgba(filmSize);
                bool badColor = false;
                for(size_t i = 0; i < filmSize; ++i) {
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
                    Imf::RgbaOutputFile out(mOutput.string().c_str(),
                                            mFilmSize.x, mFilmSize.y,
                                            Imf::WRITE_RGB);
                    out.setFrameBuffer(rgba.data(), 1, mFilmSize.x);
                    out.writePixels(mFilmSize.y);
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
