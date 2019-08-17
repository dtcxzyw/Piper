#include "DataDesc.hpp"
#include "../../PluginShared.hpp"
#include <fstream>
#pragma warning(push,0)
#include <lz4.h>
#pragma warning(pop)

//TODO:Stream Reading

template <typename T>
void read(const std::vector<char> &stream,
    uint64_t &offset, T *ptr,
    const size_t size = 1) {
    const auto rsiz = sizeof(T) * size;
    memcpy(static_cast<void *>(ptr),
        stream.data() + offset, rsiz);
    offset += rsiz;
}

std::vector<char> loadLZ4(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (in) {
        in.seekg(0, std::ios::end);
        const auto siz =
            static_cast<uint64_t>(in.tellg()) -
            sizeof(uint64_t);
        in.seekg(0);
        std::vector<char> data(siz);
        uint64_t srcSize;
        in.read(reinterpret_cast<char *>(&srcSize),
            sizeof(uint64_t));
        in.read(data.data(), siz);
        std::vector<char> res(srcSize);
        int len = LZ4_decompress_safe(
            data.data(), res.data(),
            static_cast<int>(siz),
            static_cast<int>(srcSize));
        if (len < 0 || len != srcSize)
            FATAL << "Failed to decompress LZ4 binary(error code="
            << len << ")";
        res.resize(len);
        return res;
    }
    FATAL << "Failed to load LZ4 binary " << path.string();
}

void load(const fs::path &path, optix::Buffer vertexBuf, optix::Buffer indexBuf,
    const Vec3 &scale, const Quaternion &rotate,
    const Vec3 &trans) {
    std::vector<char> data = loadLZ4(path);
    ASSERT(std::string(data.data(), data.data() + 4) ==
        "mesh", "Bad mesh header.");
    uint64_t offset = 4;  // mesh
    uint64_t vertexSize;
    read(data, offset, &vertexSize);
    vertexBuf->setSize(vertexSize);
    vertexBuf->setFormat(RT_FORMAT_USER);
    vertexBuf->setElementSize(sizeof(Vertex));
    {
        BufferMapGuard guard(vertexBuf, RT_BUFFER_MAP_WRITE_DISCARD);
        Vertex *ptr = guard.as<Vertex>();
        read(data, offset, ptr, vertexSize);
        float mat[16];
        rotate.toMatrix(mat);
        Mat4 modelTrans4 = Mat4(mat) * Mat4::scale(scale);
        Mat3 modelTrans = optix::make_matrix3x3(modelTrans4);
        Mat3 normalTrans = optix::make_matrix3x3(modelTrans4.inverse().transpose());
        for (uint64_t i = 0; i < vertexSize; ++i) {
            Vertex &vert = ptr[i];
            vert.pos = modelTrans * vert.pos + trans;
            vert.normal = normalTrans * vert.normal;
        }
    }
    uint64_t indexSize;
    read(data, offset, &indexSize);
    indexBuf->setSize(indexSize);
    indexBuf->setFormat(RT_FORMAT_UNSIGNED_INT3);
    {
        BufferMapGuard guard(vertexBuf, RT_BUFFER_MAP_WRITE_DISCARD);
        read(data, offset, guard.as<uint3>(), indexSize);
    }
}
