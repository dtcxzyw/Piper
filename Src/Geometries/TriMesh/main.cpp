#include "../../Shared/CommandAPI.hpp"
#include "../../Shared/GeometryAPI.hpp"

void load(const fs::path& path, optix::Buffer vertexBuf, optix::Buffer indexBuf,
          optix::Buffer normal, optix::Buffer texCoord, const SRT& trans,
          bool doTrans);
std::shared_ptr<Bus::ModuleFunctionBase>
getCommand(Bus::ModuleInstance& instance);

const char* moduleName = "Piper.BuiltinGeometry.TriMesh";

class TriMesh final : public Geometry {
private:
    optix::Buffer mVertex, mIndex, mNormal, mTexCoord, mMat;
    optix::GeometryTriangles mGeometryTriangle;
    optix::Geometry mGeometry;
    bool mBuiltinTriAPI;
    optix::Program mAttribute, mIntersect, mBounds;

public:
    explicit TriMesh(Bus::ModuleInstance& instance) : Geometry(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> config) override {
        BUS_TRACE_BEGIN(moduleName) {
            mBuiltinTriAPI = config->getBool("BuiltinTriangleAPI", false);
            fs::path path =
                helper->scenePath() / config->attribute("Path")->asString();
            SRT srt = config->getTransform("Transform");
            optix::Context context = helper->getContext();
            mVertex = context->createBuffer(RT_BUFFER_INPUT);
            mNormal = context->createBuffer(RT_BUFFER_INPUT);
            mTexCoord = context->createBuffer(RT_BUFFER_INPUT);
            mIndex = context->createBuffer(RT_BUFFER_INPUT);
            load(path, mVertex, mIndex, mNormal, mTexCoord, srt,
                 !mBuiltinTriAPI);
            auto mp = modulePath().parent_path();
            if(mBuiltinTriAPI) {
                mAttribute = helper->compile("meshAttributes",
                                             { "Triangle.ptx" }, mp, {}, false);
                mGeometryTriangle = context->createGeometryTriangles();
                mGeometryTriangle->setAttributeProgram(mAttribute);
                RTsize vertSize;
                mVertex->getSize(vertSize);
                mGeometryTriangle->setVertices(static_cast<unsigned>(vertSize),
                                               mVertex, RT_FORMAT_FLOAT3);
                Mat4 mat = srt.getPointTrans();
                mGeometryTriangle->setPreTransformMatrix(false, mat.getData());
                mGeometryTriangle->setTriangleIndices(mIndex,
                                                      RT_FORMAT_UNSIGNED_INT3);
                RTsize faceSize;
                mIndex->getSize(faceSize);
                mGeometryTriangle->setPrimitiveCount(
                    static_cast<unsigned>(faceSize));
                mGeometryTriangle->setMaterialCount(1);
                mMat = context->createBuffer(RT_BUFFER_INPUT);
                mMat->setFormat(RT_FORMAT_BYTE);
                mMat->setSize(faceSize);
                {
                    BufferMapGuard guard(mMat, RT_BUFFER_MAP_WRITE_DISCARD);
                    memset(guard.raw(), 0, sizeof(faceSize));
                }
                mGeometryTriangle->setMaterialIndices(mMat, 0, 1,
                                                      RT_FORMAT_BYTE);
                mGeometryTriangle->validate();
            } else {
                mGeometry = context->createGeometry();
                mGeometry["geometryVertexBuffer"]->set(mVertex);
                mGeometry["geometryIndexBuffer"]->set(mIndex);
                mGeometry["geometryTexCoordBuffer"]->set(mTexCoord);
                mGeometry["geometryNormalBuffer"]->set(mNormal);
                mBounds = helper->compile("bounds", { "Triangle.ptx" }, mp, {},
                                          false);
                mGeometry->setBoundingBoxProgram(mBounds);
                mIntersect = helper->compile("intersect", { "Triangle.ptx" },
                                             mp, {}, false);
                mGeometry->setIntersectionProgram(mIntersect);
                RTsize size;
                mIndex->getSize(size);
                mGeometry->setPrimitiveCount(static_cast<unsigned>(size));
            }
        }
        BUS_TRACE_END();
    }
    void setInstance(optix::GeometryInstance inst) override {
        BUS_TRACE_BEGIN(moduleName) {
            if(mBuiltinTriAPI) {
                inst["geometryVertexBuffer"]->set(mVertex);
                inst["geometryIndexBuffer"]->set(mIndex);
                inst["geometryTexCoordBuffer"]->set(mTexCoord);
                inst["geometryNormalBuffer"]->set(mNormal);
                inst->setGeometryTriangles(mGeometryTriangle);
            } else
                inst->setGeometry(mGeometry);
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
        if(name == "MeshCast")
            return getCommand(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
