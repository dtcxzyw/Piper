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

void cast(int argc, char **argv) {
    cxxopts::Options opt("MeshConverter", "TriMesh::MeshConverter");
    opt.allow_unrecognised_options().add_options()
        ("t,tool", "", cxxopts::value<std::string>())
        ("i,input", "mesh file path", cxxopts::value<fs::path>())
        ("o,output", "output path", cxxopts::value<fs::path>());
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
        aiProcess_FixInfacingNormals |
        aiProcess_ImproveCacheLocality);
    if (!scene ||
        scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE)
        FATAL << "Failed to load the mesh.";
    if (scene->mNumMeshes != 1)
        FATAL << "Need one mesh!!!";
    INFO << "mesh loaded.";
    const auto mesh = scene->mMeshes[0];
    INFO << mesh->mNumVertices << " vertices,"
        << mesh->mNumFaces << " faces";
    const aiVector3D *tangents = mesh->mTangents;
    std::vector<char> data{ 'm', 'e', 's', 'h' };
    uint32_t flag = 0;
    if (mesh->HasNormals()) {
        flag |= 1;
        INFO << "Has normals.";
    }
    if (mesh->HasTextureCoords(0)) {
        flag |= 2;
        INFO << "Has texCoords.";
    }
    const uint64_t vertSize = mesh->mNumVertices;
    write(data, &vertSize);
    write(data, &flag);
    write(data, mesh->mVertices, vertSize);
    if (mesh->HasNormals())
        write(data, mesh->mNormals, vertSize);
    if (mesh->HasTextureCoords(0)) {
        std::vector<aiVector2D> texCoords(vertSize);
        aiVector3D *ptr = mesh->mTextureCoords[0];
        for (uint64_t i = 0; i < vertSize; ++i)
            texCoords[i] = aiVector2D(ptr[i].x, ptr[i].y);
        write(data, texCoords.data(), vertSize);
    }
    {
        std::vector<uint3> buf(mesh->mNumFaces);
        for (auto i = 0U; i < mesh->mNumFaces; ++i)
            buf[i] = *reinterpret_cast<uint3 *>(
            mesh->mFaces[i].mIndices);
        const uint64_t faceSize = mesh->mNumFaces;
        write(data, &faceSize);
        write(data, buf.data(), buf.size());
    }
    importer.FreeScene();
    INFO << "mesh encoded.";
    saveLZ4(out, data);
    INFO << "done.";
}
