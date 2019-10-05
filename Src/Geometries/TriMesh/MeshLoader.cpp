#include "../../Shared/PluginShared.hpp"
#include <fstream>
#pragma warning(push, 0)
#include <lz4.h>
#pragma warning(pop)

// TODO:Stream Reading

BUS_MODULE_NAME("Piper.BuiltinGeometry.TriMesh");

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

void load(CUstream stream, const fs::path& path, uint64_t& vertexSize,
          uint64_t& indexSize, Buffer& vertexBuf, Buffer& indexBuf,
          Buffer& normalBuf, Buffer& texCoordBuf, Bus::Reporter& reporter) {
    BUS_TRACE_BEG() {
        std::vector<char> data = loadLZ4(path);
        ASSERT(std::string(data.data(), data.data() + 4) == "mesh",
               "Bad mesh header.");
        uint64_t offset = 4;  // mesh
        uint32_t flag = 0;
        read(data, offset, &vertexSize);
        read(data, offset, &flag);
        {
            size_t siz = vertexSize * sizeof(Vec3);
            vertexBuf = allocBuffer(siz);
            checkCudaError(cuMemcpyHtoDAsync(
                asPtr(vertexBuf), data.data() + offset, siz, stream));
            offset += siz;
        }
        // Normal
        if(flag & 1) {
            size_t siz = vertexSize * sizeof(Vec3);
            normalBuf = allocBuffer(siz);
            checkCudaError(cuMemcpyHtoDAsync(
                asPtr(normalBuf), data.data() + offset, siz, stream));
            offset += siz;
        }
        // TexCoord
        if(flag & 2) {
            size_t siz = vertexSize * sizeof(Vec2);
            texCoordBuf = allocBuffer(siz);
            checkCudaError(cuMemcpyHtoDAsync(
                asPtr(texCoordBuf), data.data() + offset, siz, stream));
            offset += siz;
        }
        read(data, offset, &indexSize);
        {
            size_t siz = indexSize * sizeof(Uint3);
            indexBuf = allocBuffer(siz);
            checkCudaError(cuMemcpyHtoDAsync(
                asPtr(indexBuf), data.data() + offset, siz, stream));
            offset += siz;
        }
        reporter.apply(ReportLevel::Info,
                       "Loaded " + std::to_string(vertexSize) + " vertexes," +
                           std::to_string(indexSize) + " faces.",
                       BUS_DEFSRCLOC());
    }
    BUS_TRACE_END();
}
