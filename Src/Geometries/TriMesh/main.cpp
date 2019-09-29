#include "../../Shared/CommandAPI.hpp"
#include "../../Shared/GeometryAPI.hpp"

void load(CUstream stream, const fs::path& path, uint64_t& vertexSize,
          uint64_t& indexSize, Buffer& vertexBuf, Buffer& indexBuf,
          Buffer& normalBuf, Buffer& texCoordBuf);
std::shared_ptr<Bus::ModuleFunctionBase>
getCommand(Bus::ModuleInstance& instance);

BUS_MODULE_NAME("Piper.BuiltinGeometry.TriMesh");

class TriMesh final : public Geometry {
private:
    Buffer mVertex, mIndex, mNormal, mTexCoord, mMat, mAccel;
    bool mBuiltinTriAPI;
    Module mModule;

public:
    explicit TriMesh(Bus::ModuleInstance& instance) : Geometry(instance) {}
    GeometryData init(PluginHelper helper, std::shared_ptr<Config> config,
                      uint32_t& hitGroupOffset) override {
        BUS_TRACE_BEG() {
            mBuiltinTriAPI = config->getBool("BuiltinTriangleAPI", false);
            fs::path path =
                helper->scenePath() / config->attribute("Path")->asString();
            SRT srt = config->getTransform("Transform");
            uint64_t vertexSize, indexSize;
            load(0, path, vertexSize, indexSize, mVertex, mIndex, mNormal,
                 mTexCoord);
            auto mp = modulePath().parent_path();
            if(mBuiltinTriAPI) {
                OptixAccelBuildOptions opt;
                opt.operation = OPTIX_BUILD_OPERATION_BUILD;
                opt.buildFlags = OPTIX_BUILD_FLAG_NONE;
                opt.motionOptions.numKeys = 0;
                opt.motionOptions.flags = OPTIX_MOTION_FLAG_NONE;
                opt.motionOptions.timeBegin = opt.motionOptions.timeEnd = 0.0f;

                OptixBuildInput input;
                input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
                OptixBuildInputTriangleArray& arr = input.triangleArray;
                OptixGeometryFlags flag = OPTIX_GEOMETRY_FLAG_NONE;
                arr.flags = &flag;
                arr.indexBuffer = asPtr(mIndex);
                arr.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
                arr.indexStrideInBytes = sizeof(uint3);
                arr.numIndexTriplets = 0;
                arr.numSbtRecords = 1;
                arr.numVertices = vertexSize;
                arr.preTransform = ;
                arr.primitiveIndexOffset = 0;
                arr.sbtIndexOffsetBuffer = ;
                arr.sbtIndexOffsetSizeInBytes = ;
                arr.sbtIndexOffsetStrideInBytes = ;
                arr.vertexBuffers = asPtr(mVertex);
                arr.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
                arr.vertexStrideInBytes = sizeof(Vec3);

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
        : Bus::ModuleInstance(path, sys) {}
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
