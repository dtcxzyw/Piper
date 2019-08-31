#include "../../Shared/PluginShared.hpp"
#include <fstream>
#pragma warning(push, 0)
#include <lz4.h>
#include <optixu/optixu_math_stream.h>
#pragma warning(pop)

// TODO:Stream Reading

template <typename T>
void read(const std::vector<char>& stream, uint64_t& offset, T* ptr,
          const size_t size = 1) {
    const auto rsiz = sizeof(T) * size;
    memcpy(static_cast<void*>(ptr), stream.data() + offset, rsiz);
    offset += rsiz;
}

extern const char* moduleName;

std::vector<char> loadLZ4(const fs::path& path) {
    BUS_TRACE_BEGIN(moduleName) {
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

void load(const fs::path& path, optix::Buffer vertexBuf, optix::Buffer indexBuf,
          optix::Buffer normal, optix::Buffer texCoord, const SRT& trans,
          bool doTrans) {
    BUS_TRACE_BEGIN(moduleName) {
        std::vector<char> data = loadLZ4(path);
        ASSERT(std::string(data.data(), data.data() + 4) == "mesh",
               "Bad mesh header.");
        uint64_t offset = 4;  // mesh
        uint64_t vertexSize;
        uint32_t flag = 0;
        read(data, offset, &vertexSize);
        read(data, offset, &flag);
        {
            vertexBuf->setSize(vertexSize);
            vertexBuf->setFormat(RT_FORMAT_FLOAT3);
            BufferMapGuard guard(vertexBuf, RT_BUFFER_MAP_WRITE_DISCARD);
            read(data, offset, guard.as<Vec3>(), vertexSize);
            if(doTrans) {
                Mat3 mat = optix::make_matrix3x3(trans.getPointTrans());
                Vec3* ptr = guard.as<Vec3>();
                for(uint64_t i = 0; i < vertexSize; ++i)
                    ptr[i] = mat * ptr[i] + trans.trans;
            }
        }
        normal->setFormat(RT_FORMAT_FLOAT3);
        if(flag & 1) {
            normal->setSize(vertexSize);
            BufferMapGuard guard(normal, RT_BUFFER_MAP_WRITE_DISCARD);
            read(data, offset, guard.as<Vec3>(), vertexSize);
            if(doTrans) {
                Mat3 mat = optix::make_matrix3x3(
                    trans.getPointTrans().inverse().transpose());
                Vec3* ptr = guard.as<Vec3>();
                for(uint64_t i = 0; i < vertexSize; ++i)
                    ptr[i] = mat * ptr[i];
            }
        }
        texCoord->setFormat(RT_FORMAT_FLOAT2);
        if(flag & 2) {
            texCoord->setSize(vertexSize);
            BufferMapGuard guard(texCoord, RT_BUFFER_MAP_WRITE_DISCARD);
            read(data, offset, guard.as<Vec2>(), vertexSize);
        }
        uint64_t indexSize;
        read(data, offset, &indexSize);
        {
            indexBuf->setSize(indexSize);
            indexBuf->setFormat(RT_FORMAT_UNSIGNED_INT3);
            BufferMapGuard guard(indexBuf, RT_BUFFER_MAP_WRITE_DISCARD);
            read(data, offset, guard.as<uint3>(), indexSize);
        }
    }
    BUS_TRACE_END();
}
