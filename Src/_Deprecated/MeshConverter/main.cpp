/*
#include <filesystem>
#include <fstream>
#pragma warning(push, 0)
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/vector3.h>
#include <cxxopts.hpp>
#include <lz4hc.h>
#pragma warning(pop)
#include "../../Common.hpp"
#include "../../Shared.hpp"

namespace fs = std::experimental::filesystem;

template <typename T>
void read(const std::vector<char> &stream,
    uint64_t &offset, T *ptr,
    const size_t size = 1) {
    const auto rsiz = sizeof(T) * size;
    memcpy(static_cast<void *>(ptr),
        stream.data() + offset, rsiz);
    offset += rsiz;
}

template <typename T>
void write(std::vector<char> &stream, const T *ptr,
    const size_t size = 1) {
    const auto begin =
        reinterpret_cast<const char *>(ptr);
    stream.insert(stream.end(), begin,
        begin + sizeof(T) * size);
}

void saveLZ4(const fs::path &path,
    const std::vector<char> &data) {
    std::ofstream out(path, std::ios::binary);
    if (out) {
        const uint64_t srcSize = data.size();
        out.write(
            reinterpret_cast<const char *>(&srcSize),
            sizeof(uint64_t));
        std::vector<char> res(LZ4_compressBound(
            static_cast<int>(data.size())));
        const auto dstSize = LZ4_compress_HC(
            data.data(), res.data(),
            static_cast<int>(srcSize),
            static_cast<int>(res.size()),
            LZ4HC_CLEVEL_MAX);
        out.write(res.data(), dstSize);
    }
    else
        FATAL << "Failed to save LZ4 binary.";
}

int main(int argc, char **argv) {
    cxxopts::Options opt("MeshConverter", "Piper-MeshConverter");
    opt.add_options()("i,input", "mesh file path",
        cxxopts::value<fs::path>())(
        "o,output", "output path",
        cxxopts::value<fs::path>());
    auto res = opt.parse(argc, argv);
    if (!(res.count("input") && res.count("output"))) {
        std::cout << opt.help() << std::endl;
        FATAL << "Need arguments.";
    }
    auto in = res["input"].as<fs::path>();
    auto out = res["output"].as<fs::path>();
    std::cout << fs::absolute(in) << "->"
        << fs::absolute(out) << std::endl;
    Assimp::Importer importer;
    const auto scene = importer.ReadFile(
        in.string(),
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_SortByPType |
        aiProcess_GenSmoothNormals |
        aiProcess_GenUVCoords |
        aiProcess_CalcTangentSpace |
        aiProcess_FixInfacingNormals |
        aiProcess_ImproveCacheLocality);
    if (!scene ||
        scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE)
        FATAL << "Failed to load the mesh.";
    if (scene->mNumMeshes != 1)
        FATAL << "Need one mesh!!!";
    DEBUG << "mesh loaded.";
    const auto mesh = scene->mMeshes[0];
    DEBUG << mesh->mNumVertices << " vertices,"
        << mesh->mNumFaces << " faces";
    if (!mesh->mNormals)
        FATAL << "No normals!!!";
    const aiVector3D *tangents = mesh->mTangents;
    std::vector<aiVector3D> fakeT;
    if (!tangents) {
        WARNING
            << "No tangents!!! Using fake tangents.";
        aiVector3D b1(0.0f, 0.0f, 1.0f),
            b2(1.0f, 0.0f, 0.0f);
        fakeT.resize(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices;
            ++i) {
            aiVector3D n = mesh->mNormals[i];
            aiVector3D b =
                (fabsf(b1 * n) < fabsf(b2 * n) ? b1 :
                b2);
            aiVector3D t = n.SymMul(b);
            fakeT[i] = t.Normalize();
        }
        tangents = fakeT.data();
    }
    std::vector<char> data{ 'm', 'e', 's', 'h' };
    {
        std::vector<Vertex> buf(mesh->mNumVertices);
        auto uv = mesh->mTextureCoords[0];
        for (auto i = 0U; i < mesh->mNumVertices; ++i) {
            buf[i].pos = *reinterpret_cast<Vec3 *>(
                mesh->mVertices + i);
            buf[i].normal = *reinterpret_cast<Vec3 *>(
                mesh->mNormals + i);
            buf[i].tangent =
                *reinterpret_cast<const Vec3 *>(
                tangents + i);
            if (uv)
                buf[i].texCoord =
                *reinterpret_cast<Vec2 *>(uv + i);
        }
        const uint64_t vertSize = mesh->mNumVertices;
        write(data, &vertSize);
        write(data, buf.data(), buf.size());
    }
    {
        std::vector<Index> buf(mesh->mNumFaces);
        for (auto i = 0U; i < mesh->mNumFaces; ++i)
            buf[i] = *reinterpret_cast<Index *>(
            mesh->mFaces[i].mIndices);
        const uint64_t faceSize = mesh->mNumFaces;
        write(data, &faceSize);
        write(data, buf.data(), buf.size());
    }
    importer.FreeScene();
    DEBUG << "mesh encoded.";
    saveLZ4(out, data);
    DEBUG << "done.";
    return 0;
}
*/
