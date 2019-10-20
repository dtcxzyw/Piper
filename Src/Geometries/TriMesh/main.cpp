#include "../../Shared/CommandAPI.hpp"
#include "../../Shared/GeometryAPI.hpp"
#include "../../Shared/MaterialAPI.hpp"
#include "DataDesc.hpp"
#include "MeshAPI.hpp"
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

std::shared_ptr<Bus::ModuleFunctionBase>
getRawMeshLoader(Bus::ModuleInstance& instance);
std::shared_ptr<Bus::ModuleFunctionBase>
getMesh2Raw(Bus::ModuleInstance& instance);

BUS_MODULE_NAME("Piper.BuiltinGeometry.TriMesh");

// TODO:Shared Accel

class TriMesh final : public Geometry {
private:
    ProgramGroup mRadGroup, mOccGroup;
    Buffer mMatIndex, mAccel;
    std::shared_ptr<Material> mMat;
    std::shared_ptr<Mesh> mMesh;
    GeometryData mData;

public:
    explicit TriMesh(Bus::ModuleInstance& instance) : Geometry(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            mMesh = helper->instantiateAsset<Mesh>(config->attribute("Mesh"));
            MeshData meshData = mMesh->getData();
            DataDesc data;
            data.vertex = static_cast<Vec3*>(meshData.vertex);
            data.index = static_cast<Uint3*>(meshData.index);
            data.normal = static_cast<Vec3*>(meshData.normal);
            data.texCoord = static_cast<Vec2*>(meshData.texCoord);
            mMat = helper->instantiateAsset<Material>(
                config->attribute("Material"));
            MaterialData matData = mMat->getData();
            data.material = helper->addCallable(matData.group, matData.radData);
            mData.maxSampleDim = matData.maxSampleDim;

            OptixModule mod = helper->loadModuleFromFile(
                modulePath().parent_path() / "Triangle.ptx");
            OptixProgramGroupDesc desc[2] = {};
            desc[0].flags = desc[1].flags = 0;
            desc[0].kind = desc[1].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
            OptixProgramGroupHitgroup& hit0 = desc[0].hitgroup;
            hit0.moduleCH = mod;
            hit0.entryFunctionNameCH = "__closesthit__RCH";
            OptixProgramGroupHitgroup& hit1 = desc[1].hitgroup;
            hit1.moduleAH = mod;
            hit1.entryFunctionNameAH = "__anyhit__OAH";
            OptixProgramGroupOptions gopt = {};
            OptixProgramGroup group[2];
            checkOptixError(optixProgramGroupCreate(
                helper->getContext(), desc, 2, &gopt, nullptr, nullptr, group));
            mRadGroup.reset(group[0]);
            mOccGroup.reset(group[1]);

            unsigned sbtID = helper->addHitGroup(
                mRadGroup.get(), packSBTRecord(mRadGroup.get(), data),
                mOccGroup.get(), packSBTRecord(mOccGroup.get(), data));

            CUstream stream = 0;

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
            arr.indexBuffer = reinterpret_cast<CUdeviceptr>(data.index);
            arr.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
            arr.indexStrideInBytes = sizeof(Uint3);
            arr.numIndexTriplets = meshData.indexSize;
            arr.numSbtRecords = 1;
            arr.numVertices = meshData.vertexSize;
            arr.preTransform = 0;
            arr.primitiveIndexOffset = 0;

            mMatIndex = allocBuffer(meshData.indexSize * sizeof(unsigned));
            checkCudaError(cuMemsetD32Async(asPtr(mMatIndex), sbtID,
                                            meshData.indexSize, stream));
            arr.sbtIndexOffsetBuffer = asPtr(mMatIndex);
            arr.sbtIndexOffsetSizeInBytes = 4;
            arr.sbtIndexOffsetStrideInBytes = 0;

            CUdeviceptr ptr = reinterpret_cast<CUdeviceptr>(meshData.vertex);
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

            checkOptixError(optixAccelBuild(
                helper->getContext(), stream, &opt, &input, 1, asPtr(tmp),
                siz.tempSizeInBytes, asPtr(mAccel), siz.outputSizeInBytes,
                &mData.handle, nullptr, 0));
            checkCudaError(cuStreamSynchronize(stream));
            // TODO:Accel Compaction
        }
        BUS_TRACE_END();
    }
    GeometryData getData() override {
        return mData;
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
        res.description = "Triangle Mesh";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Mesh::getInterface())
            return { "RawMesh" };
        if(api == Geometry::getInterface())
            return { "TriMesh" };
        if(api == Command::getInterface())
            return { "MeshConverter" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "RawMesh")
            return getRawMeshLoader(*this);
        if(name == "TriMesh")
            return std::make_shared<TriMesh>(*this);
        if(name == "MeshConverter")
            return getMesh2Raw(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
