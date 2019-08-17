#pragma warning(push, 0)
#include <cxxopts.hpp>
#include <lz4hc.h>
#include <nlohmann/json.hpp>
#define STBI_MSC_SECURE_CRT
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#pragma warning(pop)
#include "../../Common.hpp"
#include "../../Shared.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <execution>

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
            static_cast<int>(res.size()), LZ4HC_CLEVEL_MAX);
        if (dstSize == 0)
            FATAL << "Failed to compress LZ4 binary.";
        out.write(res.data(), dstSize);
    }
    else
        FATAL << "Failed to save LZ4 binary.";
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
    FATAL << "Failed to load LZ4 binary.";
    return {};
}

Spectrum ACESFitted(Spectrum c);  // sRGB->sRGB

using Json = nlohmann::json;

struct Mat33 {
    double fac[3][3];
    void readJson(Json cfg) {
        ASSERT(cfg.contains("matrix"), "Need XYZ2RGB matrix");
        cfg = cfg["matrix"];
        ASSERT(cfg.is_array() && cfg.size() == 3,
            "Bad settings");
        int id = 0;
        for (const auto &vec : cfg) {
            ASSERT(vec.is_array() && vec.size() == 3,
                "Bad settings.");
            auto beg = vec.cbegin();
            for (int i = 0; i < 3; ++i) {
                ASSERT(beg->is_number_float(),
                    "Bad settings");
                fac[id][i] = (beg++)->get<double>();
            }
            ++id;
        }
    }
    Spectrum operator()(const Spectrum &c) const {
        return make_float3(
            static_cast<float>(fac[0][0] * c.x + fac[0][1] * c.y +
            fac[0][2] * c.z),
            static_cast<float>(fac[1][0] * c.x + fac[1][1] * c.y +
            fac[1][2] * c.z),
            static_cast<float>(fac[2][0] * c.x + fac[2][1] * c.y +
            fac[2][2] * c.z));
    }
    Mat33 inverse() const {
        //from glm/glm/detail/func_matrix.inl
        double det =
            +fac[0][0] * (fac[1][1] * fac[2][2] - fac[2][1] * fac[1][2])
            - fac[1][0] * (fac[0][1] * fac[2][2] - fac[2][1] * fac[0][2])
            + fac[2][0] * (fac[0][1] * fac[1][2] - fac[1][1] * fac[0][2]);

        Mat33 res;
        res.fac[0][0] = +(fac[1][1] * fac[2][2] - fac[2][1] * fac[1][2]) / det;
        res.fac[1][0] = -(fac[1][0] * fac[2][2] - fac[2][0] * fac[1][2]) / det;
        res.fac[2][0] = +(fac[1][0] * fac[2][1] - fac[2][0] * fac[1][1]) / det;
        res.fac[0][1] = -(fac[0][1] * fac[2][2] - fac[2][1] * fac[0][2]) / det;
        res.fac[1][1] = +(fac[0][0] * fac[2][2] - fac[2][0] * fac[0][2]) / det;
        res.fac[2][1] = -(fac[0][0] * fac[2][1] - fac[2][0] * fac[0][1]) / det;
        res.fac[0][2] = +(fac[0][1] * fac[1][2] - fac[1][1] * fac[0][2]) / det;
        res.fac[1][2] = -(fac[0][0] * fac[1][2] - fac[1][0] * fac[0][2]) / det;
        res.fac[2][2] = +(fac[0][0] * fac[1][1] - fac[1][0] * fac[0][1]) / det;

        double mulv[3][3] = {};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    mulv[i][j] += fac[i][k] * res.fac[k][j];
        double mErr = 0.0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                double stdv = (i == j ? 1.0 : 0.0);
                mErr = std::max(mErr, fabs(mulv[i][j] - stdv));
            }
        DEBUG << "Inverse matrix max error:" << mErr;
        return res;
    }
};

template<typename Callable>
void applyTrans(std::vector<Spectrum> &spec, const Callable &call) {
    std::transform(std::execution::par_unseq, spec.begin(), spec.end(), spec.begin(), call);
}

int main(int argc, char **argv) {
    Json cfg;
    {
        std::ifstream settings(fs::path(argv[0]).
            replace_filename("ColorSpace.json"));
        if (!settings)
            FATAL << "Need color space settings!!!";
        try {
            settings >> cfg;
        }
        catch (const std::exception &ex) {
            FATAL << ex.what();
        }
        ASSERT(cfg.contains("sRGB"), "Need CIEXYZ(D65)2sRGB matrix.");
    }
    cxxopts::Options opt("TexConverter",
        "Piper-TexConverter");
    opt.add_options()(
        "i,input",
        "input file(.xyz,.png,.bmp,.hdr,...)",
        cxxopts::value<fs::path>())(
        "o,output", "output file(.xyz,.png,.bmp,.hdr)",
        cxxopts::value<fs::path>())(
        "c,colorspace", "color space",
        cxxopts::value<std::string>()->default_value(
        "sRGB"))("t,tonemapping",
        "tone mapping(ACES)")(
        "l,luminance", "luminance",
        cxxopts::value<float>()->default_value("1.0"));
    auto res = opt.parse(argc, argv);
    if (!(res.count("input") && res.count("output"))) {
        std::cout << opt.help() << std::endl;
        FATAL << "Need arguments.";
    }
    auto in = res["input"].as<fs::path>();
    auto out = res["output"].as<fs::path>();
    auto space = res["colorspace"].as<std::string>();
    ASSERT(cfg.contains(space), "Unrecognized color space.");
    auto toneMapping = res["tonemapping"].as<bool>();
    auto lum = res["luminance"].as<float>();
    auto ine = in.extension();
    auto oute = out.extension();
    fs::path xyz = ".xyz", png = ".png", hdr = ".hdr",
        bmp = ".bmp";
    if (ine == xyz &&
        (oute == png || oute == hdr || oute == bmp)) {
        DEBUG << ine.string() << "->" << oute.string();
        auto idt = loadLZ4(in);
        ASSERT(std::string(idt.data(), idt.data() + 3) ==
            "xyz", "Bad xyz header.");
        uint64_t offset = 3;  // xyz
        uint32_t w, h;
        read(idt, offset, &w);
        read(idt, offset, &h);
        if (idt.size() - offset !=
            sizeof(Spectrum) * w * h)
            FATAL << "Corrupted xyz file.";
        std::vector<Spectrum> spec(w * h);
        read(idt, offset, spec.data(), spec.size());
        idt.clear();
        idt.shrink_to_fit();
        DEBUG << "Process begin";
        applyTrans(spec, [=] (const Spectrum &s) {return s * lum; });
        if (toneMapping) {
            Mat33 xyz2sRGB;
            xyz2sRGB.readJson(cfg["sRGB"]);
            Mat33 sRGB2xyz = xyz2sRGB.inverse();
            applyTrans(spec, [=] (const Spectrum &s)
                {return sRGB2xyz(ACESFitted(xyz2sRGB(s))); });
        }
        {
            Mat33 xyz2rgb;
            xyz2rgb.readJson(cfg[space]);
            applyTrans(spec, [=] (const Spectrum &c) {return xyz2rgb(c); });
        }
        applyTrans(spec, [=] (const Spectrum &c) {return clamp(c, 0.0f, 1.0f); });
        std::vector<uchar3> odt;
        if (oute != hdr) {
            odt.resize(w * h);
            std::transform(std::execution::par_unseq, spec.begin(),
                spec.end(), odt.begin(), [=] (const Spectrum &c) {
                    Spectrum s = c * 255.0f;
                    int3 is = make_int3(static_cast<int>(s.x),
                        static_cast<int>(s.y),
                        static_cast<int>(s.z));
                    is = clamp(is, 0, 255);
                    return make_uchar3(is.x, is.y, is.z);
                });
        }
        DEBUG << "Process end";
        int rc = 0;
        std::string outs = out.string();
        if (oute == png) {
            rc = stbi_write_png(outs.c_str(), w, h, 3,
                odt.data(), 3 * w);
        }
        else if (oute == hdr) {
            rc = stbi_write_hdr(
                outs.c_str(), w, h, 3,
                reinterpret_cast<float *>(spec.data()));
        }
        else {
            rc = stbi_write_bmp(outs.c_str(), w, h, 3,
                odt.data());
        }
        if (rc == 0) {
            FATAL << "Failed to save image";
        }
        DEBUG << "Done.";
    }
    else if (oute == xyz) {
        DEBUG << ine.string() << "->" << oute.string();
        struct Deleter final {
            void operator()(float *ptr) const {
                stbi_image_free(ptr);
            }
        };
        using Holder = std::unique_ptr<float, Deleter>;
        std::string ins = in.string();
        int w, h, comp;
        stbi_ldr_to_hdr_gamma(1.0f);
        auto res = Holder(stbi_loadf(
            ins.c_str(), &w, &h, &comp, STBI_rgb));
        if (!res)
            FATAL << "Failed to load image:"
            << stbi_failure_reason();
        if (comp != 3)
            FATAL << "Need RGB channels!!!";
        Spectrum *spec =
            reinterpret_cast<Spectrum *>(res.get());
        size_t siz = w * h;
        Mat33 rgb2xyz;
        rgb2xyz.readJson(cfg[space]);
        rgb2xyz = rgb2xyz.inverse();
        DEBUG<<"Process begin";
        std::transform(std::execution::par_unseq, spec, spec + siz, spec,
            [=] (const Spectrum &s) {return rgb2xyz(s * lum); });
        DEBUG << "Process end";
        std::vector<char> data{ 'x', 'y', 'z' };
        write(data, &w);
        write(data, &h);
        write(data, spec, siz);
        saveLZ4(out, data);
        DEBUG << "Done.";
    }
    else {
        std::cout << opt.help() << std::endl;
        FATAL << "Unrecognized Command.";
    };
    return 0;
}
