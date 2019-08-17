#define CORRADE_DYNAMIC_PLUGIN
#include "../GeometryAPI.hpp"
#pragma warning(push,0)
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

void load(const fs::path &path, optix::Buffer vertexBuf, optix::Buffer indexBuf,
    const Vec3 &scale, const Quaternion &rotate, const Vec3 &trans);
void cast(int argc, char **argv);

class TriMesh final : public Geometry {
private:
    optix::Buffer mVertex, mIndex;
    optix::Geometry mGeometry;
    optix::Program mIntersect, mBound;
public:
    explicit TriMesh(PM::AbstractManager &manager,
        const std::string &plugin) : Geometry{ manager, plugin } {}
    void init(PluginHelper helper, JsonHelper config,
        const fs::path &modulePath) override {
        fs::path path = helper->scenePath() / config->toString("Path");
        Vec3 trans = make_float3(0.0f), scale = make_float3(1.0f);
        Quaternion rot;
        if (config->hasAttr("Transform")) {
            JsonHelper transform = config->attribute("Transform");
            trans = transform->getVec3("Trans", trans);
            scale = transform->getVec3("Scale", scale);
            if (transform->hasAttr("Rotate")) {
                JsonHelper rotate = transform->attribute("Rotate");
                if (rotate->raw().size() == 3) {
                    Vec3 euler = transform->toVec3("Rotate");
                    glm::quat q = glm::angleAxis(euler.x, glm::vec3{ 1.0f, 0.0f, 0.0f })
                        * glm::angleAxis(euler.y, glm::vec3{ 0.0f, 1.0f, 0.0f })
                        * glm::angleAxis(euler.z, glm::vec3{ 0.0f, 0.0f, 1.0f });
                    rot = Quaternion(q.x, q.y, q.z, q.w);
                    //Check
                    glm::vec3 ang = glm::eulerAngles(q);
                    ASSERT(ang.x == euler.x, "Bad quat cast");
                    ASSERT(ang.y == euler.y, "Bad quat cast");
                    ASSERT(ang.z == euler.z, "Bad quat cast");
                }
                else
                    rot = transform->toVec4("Rotate");
            }
        }
        optix::Context context = helper->getContext();
        mVertex = context->createBuffer(RT_BUFFER_INPUT);
        mIndex = context->createBuffer(RT_BUFFER_INPUT);
        load(path, mVertex, mIndex, scale, rot, trans);
        mGeometry = context->createGeometry();
        RTsize size;
        mIndex->getSize(size);
        mGeometry->setPrimitiveCount(static_cast<unsigned>(size));
        mGeometry->setPrimitiveIndexOffset(0);
        mBound = helper->compile("bounds", { "Triangle.ptx" }, modulePath, {}, false);
        mGeometry->setBoundingBoxProgram(mBound);
        mIntersect = helper->compile("intersect", { "Triangle.ptx" }, modulePath, {}, false);
        mGeometry->setIntersectionProgram(mIntersect);
        mGeometry["geometryVertex"]->set(mVertex);
        mGeometry["geometryIndex"]->set(mIndex);
        mGeometry->validate();
    }
    optix::Geometry getGeometry() override {
        return mGeometry;
    }
    void command(int argc, char **argv) override {
        cast(argc, argv);
    }
};
CORRADE_PLUGIN_REGISTER(TriMesh, TriMesh, "Piper.Geometry:1")
