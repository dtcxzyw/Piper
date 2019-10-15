#include "../../Shared/CommandAPI.hpp"
#include "../../Shared/GeometryAPI.hpp"
#include "../../Shared/MaterialAPI.hpp"
#include "DataDesc.hpp"
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

void load(CUstream stream, const fs::path& path, uint64_t& vertexSize,
          uint64_t& indexSize, Buffer& vertexBuf, Buffer& indexBuf,
          Buffer& normalBuf, Buffer& texCoordBuf, Bus::Reporter& reporter);
std::shared_ptr<Bus::ModuleFunctionBase>
getCommand(Bus::ModuleInstance& instance);

BUS_MODULE_NAME("Piper.BuiltinGeometry.TriMesh");

class TriMesh final : public Geometry {
private:
    Buffer mVertex, mIndex, mNormal, mTexCoord, mAccel, mMatIndex;
    Module mModule;
    ProgramGroup mRadGroup, mOccGroup;
    std::shared_ptr<Material> mMat;

public:
    explicit TriMesh(Bus::ModuleInstance& instance) : Geometry(instance) {}
    GeometryData init(PluginHelper helper,
                      std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            bool builtinTriAPI = config->getBool("BuiltinTriangleAPI", false);
            fs::path path =
                helper->scenePath() / config->attribute("Path")->asString();
            SRT srt = config->getTransform("Transform");
            uint64_t vertexSize, indexSize;
            CUstream stream = 0;
            load(stream, path, vertexSize, indexSize, mVertex, mIndex, mNormal,
                 mTexCoord, reporter());

            auto matCfg = config->attribute("Material");
            mMat = this->system().instantiateByName<Material>(
                matCfg->attribute("Plugin")->asString());
            MaterialData matData = mMat->init(helper, matCfg);
            DataDesc data;
            data.vertex = static_cast<Vec3*>(mVertex.get());
            data.index = static_cast<Uint3*>(mIndex.get());
            data.normal = static_cast<Vec3*>(mNormal.get());
            data.texCoord = static_cast<Vec2*>(mTexCoord.get());
            data.material = helper->addCallable(matData.group, matData.radData);
            auto mp = modulePath().parent_path();
            if(builtinTriAPI) {
                OptixAccelBuildOptions opt = {};
                opt.operation = OPTIX_BUILD_OPERATION_BUILD;
                opt.buildFlags = OPTIX_BUILD_FLAG_NONE;
                opt.motionOptions.numKeys = 1;
                opt.motionOptions.flags = OPTIX_MOTION_FLAG_NONE;
                opt.motionOptions.timeBegin = opt.motionOptions.timeEnd = 0.0f;

                OptixBuildInput input = {};
                input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
                OptixBuildInputTriangleArray& arr = input.triangleArray;
                unsigned flag = OPTIX_GEOMETRY_FLAG_NONE;
                arr.flags = &flag;
                arr.indexBuffer = asPtr(mIndex);
                arr.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
                arr.indexStrideInBytes = sizeof(Uint3);
                arr.numIndexTriplets = static_cast<unsigned>(indexSize);
                arr.numSbtRecords = 1;
                arr.numVertices = static_cast<unsigned>(vertexSize);
                Mat4 trans4 = srt.getPointTrans();
                glm::mat3x4 trans = trans4;
                Buffer dTrans = uploadParam(
                    stream, trans, OPTIX_GEOMETRY_TRANSFORM_BYTE_ALIGNMENT);
                arr.preTransform = asPtr(dTrans);
                // TODO:transform normal
                arr.primitiveIndexOffset = 0;
                mMatIndex = allocBuffer(indexSize * sizeof(int));
                checkCudaError(
                    cuMemsetD32Async(asPtr(mMatIndex), 0, indexSize, stream));
                arr.sbtIndexOffsetBuffer = asPtr(mMatIndex);
                arr.sbtIndexOffsetSizeInBytes = 4;
                arr.sbtIndexOffsetStrideInBytes = 0;
                CUdeviceptr ptr = asPtr(mVertex);
                arr.vertexBuffers = &ptr;
                arr.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
                arr.vertexStrideInBytes = sizeof(Vec3);

                OptixAccelBufferSizes siz;
                checkOptixError(optixAccelComputeMemoryUsage(
                    helper->getContext(), &opt, &input, 1, &siz));

                Buffer tmp = allocBuffer(siz.tempSizeInBytes,
                                         OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT);
                mAccel = allocBuffer(siz.outputSizeInBytes,
                                     OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT);

                GeometryData res;
                res.maxSampleDim = matData.maxSampleDim;

                checkOptixError(optixAccelBuild(
                    helper->getContext(), stream, &opt, &input, 1, asPtr(tmp),
                    siz.tempSizeInBytes, asPtr(mAccel), siz.outputSizeInBytes,
                    &res.handle, nullptr, 0));
                checkCudaError(cuStreamSynchronize(stream));
                tmp.reset(nullptr);
                // TODO:Accel Compaction

                mModule = helper->compileFile(modulePath().parent_path() /
                                              "Triangle.ptx");
                OptixProgramGroupDesc desc[2] = {};
                desc[0].flags = desc[1].flags = 0;
                desc[0].kind = desc[1].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
                OptixProgramGroupHitgroup& hit0 = desc[0].hitgroup;
                hit0.moduleCH = mModule.get();
                hit0.entryFunctionNameCH = "__closesthit__RCH";
                OptixProgramGroupHitgroup& hit1 = desc[1].hitgroup;
                hit1.moduleAH = mModule.get();
                hit1.entryFunctionNameAH = "__anyhit__OAH";
                OptixProgramGroupOptions gopt = {};
                OptixProgramGroup group[2];
                checkOptixError(optixProgramGroupCreate(helper->getContext(),
                                                        desc, 2, &gopt, nullptr,
                                                        nullptr, group));
                mRadGroup.reset(group[0]);
                mOccGroup.reset(group[1]);

                res.radSBTData = packSBTRecord(mRadGroup.get(), data);
                res.occSBTData = packSBTRecord(mOccGroup.get(), data);
                res.group.assign(group, group + 2);
                return res;
            } else {
                BUS_TRACE_THROW(std::logic_error("unimplemented feature"));
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
        res.guid = Bus::str2GUID("{E5BCA3C5-582B-408E-ACB2-6000C821995E}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = "TriMesh";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Geometry::getInterface())
            return { "TriMesh" };
        if(api == Command::getInterface())
            return { "MeshConverter" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "TriMesh")
            return std::make_shared<TriMesh>(*this);
        if(name == "MeshConverter")
            return getCommand(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
