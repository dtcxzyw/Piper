#include "../../Shared/CommandAPI.hpp"
#include "../../Shared/GeometryAPI.hpp"
#include "../../Shared/MaterialAPI.hpp"
#include "DataDesc.hpp"
#include "MeshAPI.hpp"
#pragma warning(push, 0)
#define NOMINMAX
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

std::shared_ptr<Bus::ModuleFunctionBase>
getRawMeshLoader(Bus::ModuleInstance& instance);
std::shared_ptr<Bus::ModuleFunctionBase>
getMesh2Raw(Bus::ModuleInstance& instance);

BUS_MODULE_NAME("Piper.BuiltinGeometry.TriMesh");

struct TriMeshAccelData final {
    OptixTraversableHandle handle;
    OptixProgramGroup radGroup, occGroup;
    DataDesc accelData;
    OptixAabb aabb;
    unsigned cssRad, cssOcc;
};

class TriMeshAccel final : public Asset {
private:
    Buffer mAccel;
    std::shared_ptr<Mesh> mMesh;
    ProgramGroup mRadGroup, mOccGroup;
    TriMeshAccelData mData;

public:
    static Name getInterface() {
        return "Piper.TriMeshAccel:1";
    }
    explicit TriMeshAccel(Bus::ModuleInstance& instance) : Asset(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            mMesh = helper->instantiateAsset<Mesh>(config->attribute("Mesh"));
            MeshData meshData = mMesh->getData();
            DataDesc& data = mData.accelData;
            data.vertex = static_cast<Vec3*>(meshData.vertex);
            data.index = static_cast<Uint3*>(meshData.index);
            data.normal = static_cast<Vec3*>(meshData.normal);
            data.texCoord = static_cast<Vec2*>(meshData.texCoord);

            const ModuleDesc& mod =
                helper->getModuleManager()->getModuleFromFile(
                    modulePath().parent_path() / "Triangle.ptx");
            OptixProgramGroupDesc desc[2] = {};
            desc[0].flags = desc[1].flags = 0;
            desc[0].kind = desc[1].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
            OptixProgramGroupHitgroup& hit0 = desc[0].hitgroup;
            hit0.moduleCH = mod.handle.get();
            hit0.entryFunctionNameCH = mod.map("__closesthit__RCH");
            OptixProgramGroupHitgroup& hit1 = desc[1].hitgroup;
            hit1.moduleAH = mod.handle.get();
            hit1.entryFunctionNameAH = mod.map("__anyhit__OAH");
            OptixProgramGroupOptions gopt = {};
            OptixProgramGroup group[2];
            checkOptixError(optixProgramGroupCreate(
                helper->getContext(), desc, 2, &gopt, nullptr, nullptr, group));
            mRadGroup.reset(group[0]);
            mOccGroup.reset(group[1]);
            mData.radGroup = mRadGroup.get();
            mData.occGroup = mOccGroup.get();
            OptixStackSizes size;
            checkOptixError(
                optixProgramGroupGetStackSize(mData.radGroup, &size));
            mData.cssRad =
                std::max(size.cssAH, std::max(size.cssCH, size.cssIS));
            checkOptixError(
                optixProgramGroupGetStackSize(mData.occGroup, &size));
            mData.cssOcc =
                std::max(size.cssAH, std::max(size.cssCH, size.cssIS));

            CUstream stream = 0;

            OptixAccelBuildOptions opt = {};
            opt.operation = OPTIX_BUILD_OPERATION_BUILD;
            opt.buildFlags = OPTIX_BUILD_FLAG_ALLOW_COMPACTION |
                OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
            opt.motionOptions.numKeys = 1;

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

            arr.sbtIndexOffsetBuffer = 0;
            arr.sbtIndexOffsetSizeInBytes = 0;
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
            Buffer ucAccel = allocBuffer(siz.outputSizeInBytes,
                                         OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT);
            OptixAccelEmitDesc emit[2];
            emit[0].type = OPTIX_PROPERTY_TYPE_AABBS;
            Buffer aabb = allocBuffer(sizeof(OptixAabb));
            emit[0].result = asPtr(aabb);

            emit[1].type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
            Buffer csizd = allocBuffer(sizeof(uint64_t));
            emit[1].result = asPtr(csizd);

            OptixTraversableHandle ucHandle;
            checkOptixError(
                optixAccelBuild(helper->getContext(), stream, &opt, &input, 1,
                                asPtr(tmp), siz.tempSizeInBytes, asPtr(ucAccel),
                                siz.outputSizeInBytes, &ucHandle, emit, 2));
            checkCudaError(cuStreamSynchronize(stream));
            // TODO:use stream
            mData.aabb = downloadData<OptixAabb>(aabb, 0);
            uint64_t csiz = downloadData<uint64_t>(csizd, 0);
            mAccel = allocBuffer(csiz, OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT);
            checkOptixError(optixAccelCompact(helper->getContext(), stream,
                                              ucHandle, asPtr(mAccel), csiz,
                                              &mData.handle));
            checkCudaError(cuStreamSynchronize(stream));
        }
        BUS_TRACE_END();
    }
    TriMeshAccelData getData() const {
        return mData;
    }
};

class TriMesh final : public Geometry {
private:
    std::shared_ptr<TriMeshAccel> mAccel;
    std::shared_ptr<Material> mMat;
    GeometryData mData;
    Buffer mAccelBuffer, mInstance;

public:
    explicit TriMesh(Bus::ModuleInstance& instance) : Geometry(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            mAccel = helper->instantiateAsset<TriMeshAccel>(
                config->attribute("Accel"));
            mMat = helper->instantiateAsset<Material>(
                config->attribute("Material"));
            TriMeshAccelData accelData = mAccel->getData();
            DataDesc data = accelData.accelData;
            MaterialData matData = mMat->getData();
            data.material = helper->addCallable(matData.group, matData.radData);
            mData.maxSampleDim = matData.maxSampleDim;

            unsigned sbtID = helper->addHitGroup(
                accelData.radGroup, packSBTRecord(accelData.radGroup, data),
                accelData.occGroup, packSBTRecord(accelData.occGroup, data));

            // TODO:Transform
            // if(sbtID != 0)
            OptixInstance inst = {};
            // TODO:mask
            inst.visibilityMask = 255;
            inst.instanceId = 0;
            // inst.flags = OPTIX_INSTANCE_FLAG_DISABLE_TRANSFORM;
            inst.flags = OPTIX_INSTANCE_FLAG_NONE;
            inst.sbtOffset = sbtID * 2;
            inst.traversableHandle = accelData.handle;
            // TODO:Disable transform???
            *reinterpret_cast<glm::mat3x4*>(inst.transform) =
                glm::identity<Mat4>();
            mInstance = uploadData(0, &inst, 1, OPTIX_INSTANCE_BYTE_ALIGNMENT);

            OptixBuildInput input = {};
            input.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
            auto& instInput = input.instanceArray;
            instInput.aabbs = instInput.numAabbs = 0;
            instInput.numInstances = 1;
            instInput.instances = asPtr(mInstance);

            OptixAccelBuildOptions opt = {};
            opt.operation = OPTIX_BUILD_OPERATION_BUILD;
            opt.motionOptions.numKeys = 1;

            OptixAccelBufferSizes size;

            checkOptixError(optixAccelComputeMemoryUsage(
                helper->getContext(), &opt, &input, 1, &size));

            Buffer tmp = allocBuffer(size.tempSizeInBytes,
                                     OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT);
            mAccelBuffer = allocBuffer(size.outputSizeInBytes,
                                       OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT);
            checkOptixError(optixAccelBuild(
                helper->getContext(), 0, &opt, &input, 1, asPtr(tmp),
                size.tempSizeInBytes, asPtr(mAccelBuffer),
                size.outputSizeInBytes, &mData.handle, nullptr, 0));
            checkCudaError(cuStreamSynchronize(0));

            mData.cssOcc = accelData.cssOcc;
            mData.cssRad = accelData.cssRad + matData.css;
            mData.dssS = matData.dss;
            mData.dssT = 0;
            mData.graphHeight = 2;
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
        if(api == TriMeshAccel::getInterface())
            return { "TriMeshAccel" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "RawMesh")
            return getRawMeshLoader(*this);
        if(name == "TriMeshAccel")
            return std::make_shared<TriMeshAccel>(*this);
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
