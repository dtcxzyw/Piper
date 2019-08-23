#define CORRADE_DYNAMIC_PLUGIN
#include "../GeometryAPI.hpp"

void load(const fs::path &path, optix::Buffer vertexBuf, optix::Buffer indexBuf,
    optix::Buffer normal, optix::Buffer texCoord, const SRT &trans, bool doTrans);
void cast(int argc, char **argv);

class TriMesh final : public Geometry {
private:
    optix::Buffer mVertex, mIndex, mNormal, mTexCoord, mMat;
    optix::GeometryTriangles mGeometryTriangle;
    optix::Geometry mGeometry;
    bool mBuiltinTriAPI;
    optix::Program mAttribute, mIntersect, mBounds;
public:
    explicit TriMesh(PM::AbstractManager &manager,
        const std::string &plugin) : Geometry{ manager, plugin } {}
    void init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) override {
        mBuiltinTriAPI = config->getBool("BuiltinTriangleAPI", false);
        fs::path path = helper->scenePath() / config->toString("Path");
        SRT srt = config->getTransform("Transform");
        optix::Context context = helper->getContext();
        mVertex = context->createBuffer(RT_BUFFER_INPUT);
        mNormal = context->createBuffer(RT_BUFFER_INPUT);
        mTexCoord = context->createBuffer(RT_BUFFER_INPUT);
        mIndex = context->createBuffer(RT_BUFFER_INPUT);
        load(path, mVertex, mIndex, mNormal, mTexCoord, srt, !mBuiltinTriAPI);
        if (mBuiltinTriAPI) {
            mAttribute = helper->compile("meshAttributes", { "Triangle.ptx" }, modulePath, {}, false);
            mGeometryTriangle = context->createGeometryTriangles();
            mGeometryTriangle->setAttributeProgram(mAttribute);
            RTsize vertSize;
            mVertex->getSize(vertSize);
            mGeometryTriangle->setVertices(static_cast<unsigned>(vertSize), mVertex, RT_FORMAT_FLOAT3);
            Mat4 mat = srt.getPointTrans();
            mGeometryTriangle->setPreTransformMatrix(false, mat.getData());
            mGeometryTriangle->setTriangleIndices(mIndex, RT_FORMAT_UNSIGNED_INT3);
            RTsize faceSize;
            mIndex->getSize(faceSize);
            mGeometryTriangle->setPrimitiveCount(static_cast<unsigned>(faceSize));
            mGeometryTriangle->setMaterialCount(1);
            mMat = context->createBuffer(RT_BUFFER_INPUT);
            mMat->setFormat(RT_FORMAT_BYTE);
            mMat->setSize(faceSize);
            {
                BufferMapGuard guard(mMat, RT_BUFFER_MAP_WRITE_DISCARD);
                memset(guard.raw(), 0, sizeof(faceSize));
            }
            mGeometryTriangle->setMaterialIndices(mMat, 0, 1, RT_FORMAT_BYTE);
            mGeometryTriangle->validate();
        }
        else {
            mGeometry = context->createGeometry();
            mGeometry["geometryVertexBuffer"]->set(mVertex);
            mGeometry["geometryIndexBuffer"]->set(mIndex);
            mGeometry["geometryTexCoordBuffer"]->set(mTexCoord);
            mGeometry["geometryNormalBuffer"]->set(mNormal);
            mBounds = helper->compile("bounds", { "Triangle.ptx" },
                modulePath, {}, false);
            mGeometry->setBoundingBoxProgram(mBounds);
            mIntersect = helper->compile("intersect", { "Triangle.ptx" },
                modulePath, {}, false);
            mGeometry->setIntersectionProgram(mIntersect);
            RTsize size;
            mIndex->getSize(size);
            mGeometry->setPrimitiveCount(static_cast<unsigned>(size));
        }
    }
    void setInstance(optix::GeometryInstance inst) override {
        if (mBuiltinTriAPI) {
            inst["geometryVertexBuffer"]->set(mVertex);
            inst["geometryIndexBuffer"]->set(mIndex);
            inst["geometryTexCoordBuffer"]->set(mTexCoord);
            inst["geometryNormalBuffer"]->set(mNormal);
            inst->setGeometryTriangles(mGeometryTriangle);
        }
        else 
            inst->setGeometry(mGeometry);
    }
    void command(int argc, char **argv) override {
        cast(argc, argv);
    }
};
CORRADE_PLUGIN_REGISTER(TriMesh, TriMesh, "Piper.Geometry:1")
