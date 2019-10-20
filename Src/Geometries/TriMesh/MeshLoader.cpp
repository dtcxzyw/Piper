#include "MeshAPI.hpp"
#include <fstream>
#pragma warning(push, 0)
#include <lz4.h>
#pragma warning(pop)

// TODO:Stream Reading

BUS_MODULE_NAME("Piper.BuiltinGeometry.TriMesh.RawMeshLoader");

template <typename T>
void read(const std::vector<char>& stream, uint64_t& offset, T* ptr,
          const size_t size = 1) {
    const auto rsiz = sizeof(T) * size;
    memcpy(static_cast<void*>(ptr), stream.data() + offset, rsiz);
    offset += rsiz;
}

std::vector<char> loadLZ4(const fs::path& path) {
    BUS_TRACE_BEG() {
        std::ifstream in(path, std::ios::binary);
        ASSERT(in, "Failed to load LZ4 binary " + path.string());
        in.seekg(0, std::ios::end);
        const auto siz = static_cast<uint64_t>(in.tellg()) - sizeof(uint64_t);
        in.seekg(0);
        std::vector<char> data(siz);
        uint64_t srcSize;
        in.read(reinterpret_cast<char*>(&srcSize), sizeof(uint64_t));
        in.read(data.data(), siz);
        std::vector<char> res(srcSize);
        int len =
            LZ4_decompress_safe(data.data(), res.data(), static_cast<int>(siz),
                                static_cast<int>(srcSize));
        if(len < 0 || len != srcSize)
            BUS_TRACE_THROW(std::runtime_error(
                "Failed to decompress LZ4 binary(error code=" +
                std::to_string(len) + ")"));
        res.resize(len);
        return res;
    }
    BUS_TRACE_END();
}

class RawMesh final : public Mesh {
private:
    uint32_t mVertexSize, mIndexSize;
    Buffer mVertexBuf, mIndexBuf, mNormalBuf, mTexCoordBuf;

public:
    explicit RawMesh(Bus::ModuleInstance& instance) : Mesh(instance) {}
    void init(PluginHelper helper, std::shared_ptr<Config> config) override {
        BUS_TRACE_BEG() {
            fs::path path = config->attribute("Path")->asString();
            CUstream stream = 0;

            SRT transform = config->getTransform("Transform");
            // TODO:pre transform

            std::vector<char> data = loadLZ4(path);
            ASSERT(std::string(data.data(), data.data() + 4) == "mesh",
                   "Bad mesh header.");
            uint64_t offset = 4;  // mesh
            uint32_t flag = 0;
            read(data, offset, &mVertexSize);
            read(data, offset, &flag);
            {
                size_t siz = mVertexSize * sizeof(Vec3);
                mVertexBuf = allocBuffer(siz);
                checkCudaError(cuMemcpyHtoDAsync(
                    asPtr(mVertexBuf), data.data() + offset, siz, stream));
                offset += siz;
            }
            // Normal
            if(flag & 1) {
                size_t siz = mVertexSize * sizeof(Vec3);
                mNormalBuf = allocBuffer(siz);
                checkCudaError(cuMemcpyHtoDAsync(
                    asPtr(mNormalBuf), data.data() + offset, siz, stream));
                offset += siz;
            }
            // TexCoord
            if(flag & 2) {
                size_t siz = mVertexSize * sizeof(Vec2);
                mTexCoordBuf = allocBuffer(siz);
                checkCudaError(cuMemcpyHtoDAsync(
                    asPtr(mTexCoordBuf), data.data() + offset, siz, stream));
                offset += siz;
            }
            read(data, offset, &mIndexSize);
            {
                size_t siz = mIndexSize * sizeof(Uint3);
                mIndexBuf = allocBuffer(siz);
                checkCudaError(cuMemcpyHtoDAsync(
                    asPtr(mIndexBuf), data.data() + offset, siz, stream));
                offset += siz;
            }
            reporter().apply(ReportLevel::Info,
                             "Loaded " + std::to_string(mVertexSize) +
                                 " vertexes," + std::to_string(mIndexSize) +
                                 " faces.",
                             BUS_DEFSRCLOC());
        }
        BUS_TRACE_END();
    }
    MeshData getData() override {
        MeshData data;
        data.index = mIndexBuf.get();
        data.indexSize = mIndexSize;
        data.normal = mNormalBuf.get();
        data.texCoord = mTexCoordBuf.get();
        data.vertex = mVertexBuf.get();
        data.vertexSize = mVertexSize;
        return data;
    }
};

std::shared_ptr<Bus::ModuleFunctionBase>
getRawMeshLoader(Bus::ModuleInstance& instance) {
    return std::make_shared<RawMesh>(instance);
}
