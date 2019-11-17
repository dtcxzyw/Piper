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
#include "../../Shared/CommandAPI.hpp"

BUS_MODULE_NAME("Piper.BuiltinGeometry.TriMesh.MeshConverter");

template <typename T>
void read(const std::vector<char>& stream, uint64_t& offset, T* ptr,
          const size_t size = 1) {
    const auto rsiz = sizeof(T) * size;
    memcpy(static_cast<void*>(ptr), stream.data() + offset, rsiz);
    offset += rsiz;
}

template <typename T>
void write(std::vector<char>& stream, const T* ptr, const size_t size = 1) {
    const auto begin = reinterpret_cast<const char*>(ptr);
    stream.insert(stream.end(), begin, begin + sizeof(T) * size);
}

void saveLZ4(const fs::path& path, const std::vector<char>& data) {
    BUS_TRACE_BEG() {
        std::ofstream out(path, std::ios::binary);
        ASSERT(out, "Failed to save LZ4 binary");
        const uint64_t srcSize = data.size();
        out.write(reinterpret_cast<const char*>(&srcSize), sizeof(uint64_t));
        std::vector<char> res(LZ4_compressBound(static_cast<int>(data.size())));
        const auto dstSize =
            LZ4_compress_HC(data.data(), res.data(), static_cast<int>(srcSize),
                            static_cast<int>(res.size()), LZ4HC_CLEVEL_MAX);
        out.write(res.data(), dstSize);
    }
    BUS_TRACE_END();
}

int cast(int argc, char** argv, Bus::Reporter& reporter) {
    BUS_TRACE_BEG() {
        cxxopts::Options opt("MeshConverter", "TriMesh::MeshConverter");
        opt.add_options()("i,input", "mesh file path",
                          cxxopts::value<fs::path>())(
            "o,output", "output path", cxxopts::value<fs::path>());
        auto res = opt.parse(argc, argv);
        if(!(res.count("input") && res.count("output"))) {
            reporter.apply(ReportLevel::Error, "Need Arguments.",
                           BUS_DEFSRCLOC());
            reporter.apply(ReportLevel::Info, opt.help(), BUS_DEFSRCLOC());
            return EXIT_FAILURE;
        }
        auto in = res["input"].as<fs::path>();
        auto out = res["output"].as<fs::path>();
        reporter.apply(ReportLevel::Info,
                       fs::absolute(in).string() + "->" +
                           fs::absolute(out).string(),
                       BUS_DEFSRCLOC());
        Assimp::Importer importer;
        const auto scene = importer.ReadFile(
            in.string(),
            aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                aiProcess_SortByPType | aiProcess_GenSmoothNormals |
                aiProcess_GenUVCoords | aiProcess_FixInfacingNormals |
                aiProcess_ImproveCacheLocality);
        if(!scene || scene->mFlags == AI_SCENE_FLAGS_INCOMPLETE)
            BUS_TRACE_THROW(std::runtime_error("Failed to load the mesh."));
        if(scene->mNumMeshes != 1)
            BUS_TRACE_THROW(std::runtime_error("Need one mesh!!!"));
        reporter.apply(ReportLevel::Info, "mesh loaded.", BUS_DEFSRCLOC());
        const auto mesh = scene->mMeshes[0];
        reporter.apply(ReportLevel::Info,
                       std::to_string(mesh->mNumVertices) + " vertices," +
                           std::to_string(mesh->mNumFaces) + " faces.",
                       BUS_DEFSRCLOC());
        const aiVector3D* tangents = mesh->mTangents;
        std::vector<char> data{ 'm', 'e', 's', 'h' };
        uint32_t flag = 0;
        if(mesh->HasNormals()) {
            flag |= 1;
            reporter.apply(ReportLevel::Info, "Has normals.", BUS_DEFSRCLOC());
        }
        if(mesh->HasTextureCoords(0)) {
            flag |= 2;
            reporter.apply(ReportLevel::Info, "Has texCoords.",
                           BUS_DEFSRCLOC());
        }
        const uint32_t vertSize = mesh->mNumVertices;
        write(data, &vertSize);
        write(data, &flag);
        write(data, mesh->mVertices, vertSize);
        if(mesh->HasNormals())
            write(data, mesh->mNormals, vertSize);
        if(mesh->HasTextureCoords(0)) {
            std::vector<aiVector2D> texCoords(vertSize);
            aiVector3D* ptr = mesh->mTextureCoords[0];
            for(uint32_t i = 0; i < vertSize; ++i)
                texCoords[i] = aiVector2D(ptr[i].x, ptr[i].y);
            write(data, texCoords.data(), vertSize);
        }
        {
            std::vector<Uint3> buf(mesh->mNumFaces);
            for(auto i = 0U; i < mesh->mNumFaces; ++i)
                buf[i] = *reinterpret_cast<Uint3*>(mesh->mFaces[i].mIndices);
            const uint32_t faceSize = mesh->mNumFaces;
            write(data, &faceSize);
            write(data, buf.data(), buf.size());
        }
        importer.FreeScene();
        reporter.apply(ReportLevel::Info, "Mesh encoded.", BUS_DEFSRCLOC());
        saveLZ4(out, data);
        reporter.apply(ReportLevel::Info, "Done.", BUS_DEFSRCLOC());
        return EXIT_SUCCESS;
    }
    BUS_TRACE_END();
}

class MeshConverter final : public Command {
public:
    explicit MeshConverter(Bus::ModuleInstance& instance) : Command(instance) {}
    int doCommand(int argc, char** argv, Bus::ModuleSystem& sys) override {
        return cast(argc, argv, sys.getReporter());
    }
};

std::shared_ptr<Bus::ModuleFunctionBase>
getMesh2Raw(Bus::ModuleInstance& instance) {
    return std::make_shared<MeshConverter>(instance);
}
