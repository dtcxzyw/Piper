#include "../../Shared/ConfigAPI.hpp"
#include "../../Shared/SamplerAPI.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#pragma warning(push, 0)
#define NOMINMAX
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

// http://gruenschloss.org/halton/halton.zip
// Enumerating Quasi-Monte Carlo Point Sequences in Elementary Intervals
// http://gruenschloss.org/sample-enum/sample-enum.pdf

// Copyright (c) 2012 Leonhard Gruenschloss (leonhard@gruenschloss.org)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

BUS_MODULE_NAME("Piper.BuiltinSampler.Halton");

std::vector<unsigned> getPrimeTable(unsigned count) {
    std::vector<unsigned> res;
    for(unsigned i = 2; static_cast<unsigned>(res.size()) < count; ++i) {
        bool flag = true;
        for(auto x : res)
            if(i % x == 0) {
                flag = false;
                break;
            }
        if(flag)
            res.emplace_back(i);
    }
    return res;
}

std::vector<std::vector<unsigned>> initFaure(unsigned maxBase) {
    std::vector<std::vector<unsigned>> perms(maxBase + 1);
    // Keep identity permutations for base 1, 2, 3.
    for(unsigned k = 1; k <= 3; ++k) {
        perms[k].resize(k);
        for(unsigned i = 0; i < k; ++i)
            perms[k][i] = i;
    }
    for(unsigned base = 4; base <= maxBase; ++base) {
        perms[base].resize(base);
        const unsigned b = base / 2;
        if(base & 1)  // odd
        {
            for(unsigned i = 0; i < base - 1; ++i)
                perms[base][i + (i >= b)] =
                    perms[base - 1][i] + (perms[base - 1][i] >= b);
            perms[base][b] = b;
        } else  // even
        {
            for(unsigned i = 0; i < b; ++i) {
                perms[base][i] = 2 * perms[b][i];
                perms[base][b + i] = 2 * perms[b][i] + 1;
            }
        }
    }
    return perms;
}

template <typename RNG>
std::vector<std::vector<unsigned>> initRandom(unsigned maxBase, RNG& eng) {
    std::vector<std::vector<unsigned>> perms(maxBase + 1);
    // Keep identity permutations for base 1, 2, 3.
    for(unsigned k = 1; k <= 3; ++k) {
        perms[k].resize(k);
        for(unsigned i = 0; i < k; ++i)
            perms[k][i] = i;
    }
    for(unsigned base = 4; base <= maxBase; ++base) {
        perms[base].resize(base);
        for(unsigned i = 0; i < base; ++i)
            perms[base][i] = i;
        std::shuffle(perms[base].begin(), perms[base].end(), eng);
    }
    return perms;
}

unsigned invert(unsigned base, unsigned digits, unsigned index,
                const std::vector<unsigned>& perm) {
    unsigned result = 0;
    for(unsigned i = 0; i < digits; ++i) {
        result = result * base + perm[index % base];
        index /= base;
    }
    return result;
}

void genCode(std::stringstream& ss, const std::vector<unsigned>& perms,
             unsigned base, unsigned maxTableSize) {
    // Special case: radical inverse in base 2, with direct bit reversal.
    if(base == 2) {
        ss << R"#(extern "C" __device__ float __direct_callable__sample2(unsigned idx) {
    idx = (idx << 16) | (idx >> 16);
    idx = ((idx & 0x00ff00ff) << 8) | ((idx & 0xff00ff00) >> 8);
    idx = ((idx & 0x0f0f0f0f) << 4) | ((idx & 0xf0f0f0f0) >> 4);
    idx = ((idx & 0x33333333) << 2) | ((idx & 0xcccccccc) >> 2);
    idx = ((idx & 0x55555555) << 1) | ((idx & 0xaaaaaaaa) >> 1);
    union Result {
        unsigned u;
        float f;
    } res;
    res.u = 0x3f800000u | (idx >> 9);
    return res.f - 1.0f;
}
)#";
    } else {
        unsigned digits = 1, powBase = base;
        while(powBase * base <= maxTableSize)
            powBase *= base, ++digits;

        uint64_t maxPower = powBase;
        while(maxPower * powBase < (1ULL << 32))  // 32-bit unsigned precision
            maxPower *= powBase;
        uint64_t power = maxPower / powBase;
        ss << "static __device__ __constant__ unsigned LUT" << base << '['
           << powBase << "] = {";
        for(unsigned i = 0; i < powBase; ++i) {
            if(i)
                ss << ',';
            ss << invert(base, digits, i, perms);
        }
        ss << "};\nextern \"C\" __device__ float __direct_callable__sample"
           << base << "(unsigned idx) {";
        ss << "return (LUT" << base << "[idx % " << powBase << "u] * " << power
           << "u + ";
        unsigned div = 1;
        while(power > powBase) {
            div *= powBase;
            power /= powBase;
            ss << "LUT" << base << "[(idx/" << div << "u) % " << powBase
               << "u] * " << power << "u + ";
        }
        ss << "LUT" << base << "[(idx/" << div * powBase << "u) % " << powBase
           << "u]) * static_cast<float>(" << (0x1.fffffcp-1 / maxPower)
           << ");}\n";
    }
}

void generateSample(std::stringstream& out, unsigned maxDim,
                    std::shared_ptr<Config> config, Bus::Reporter& reporter,
                    const std::vector<unsigned>& primeTable) {
    BUS_TRACE_BEG() {
        if(maxDim > 256)
            BUS_TRACE_THROW(std::runtime_error("Need MaxDim<=256"));
        std::string initType = config->getString("Type", "Faure");
        unsigned maxTableSize = config->getUint("MaxPermTableSize", 500U);
        if(maxTableSize > 65536)
            BUS_TRACE_THROW(std::runtime_error("Need MaxPermTableSize<=65536"));
        std::vector<std::vector<unsigned>> perms;
        if(initType == "Faure")
            perms = initFaure(primeTable.back());
        else {
            std::stringstream ss;
            ss << initType;
            std::string type;
            uint64_t seed = -1;
            ss >> type >> seed;
            if(seed == -1)
                seed = std::chrono::high_resolution_clock::now()
                           .time_since_epoch()
                           .count();

            if(type == "LinearCongruential") {
                std::minstd_rand rng(static_cast<unsigned>(seed));
                perms = initRandom(primeTable.back(), rng);
            } else if(type == "MersenneTwister") {
                std::mt19937_64 rng(seed);
                perms = initRandom(primeTable.back(), rng);
            } else if(type == "SubtractWithCarry") {
                std::ranlux48 rng(seed);
                perms = initRandom(primeTable.back(), rng);
            } else if(type == "Device") {
                std::random_device dev;
                if(dev.entropy() == 0.0)
                    BUS_TRACE_THROW(
                        std::runtime_error("Random Device is not available."));
                else
                    perms = initRandom(primeTable.back(), dev);
            }
        }
        out.precision(15);
        for(unsigned i = 0; i < maxDim; ++i) {
            unsigned base = primeTable[i];
            genCode(out, perms[base], base, maxTableSize);
        }
    }
    BUS_TRACE_END();
}

struct DataDesc final {
    unsigned p2, p3, x, y, increment;
    float scaleX, scaleY;
};

static inline std::pair<int, int> extendedEuclid(const int a, const int b) {
    if(!b)
        return std::make_pair(1u, 0u);
    const int q = a / b;
    const int r = a % b;
    const std::pair<int, int> st = extendedEuclid(b, r);
    return std::make_pair(st.second, st.first - q * st.second);
}

unsigned generateInit(std::stringstream& ss, Uint2 size, DataDesc& desc) {
    desc.p2 = 0;
    // Find 2^p2 >= width.
    unsigned w = 1;
    while(w < size.x) {
        ++desc.p2;
        w *= 2;
    }
    desc.scaleX = static_cast<float>(w);

    desc.p3 = 0;
    // Find 3^p3 >= height.
    unsigned h = 1;
    while(h < size.y) {
        ++desc.p3;
        h *= 3;
    }
    desc.scaleY = static_cast<float>(h);
    desc.increment = w * h;  // There's exactly one sample per pixel.

    // Determine the multiplicative inverses.
    const std::pair<int, int> inv = extendedEuclid(h, w);
    const unsigned inv2 = (inv.first < 0) ? (inv.first + w) : (inv.first % w);
    const unsigned inv3 =
        (inv.second < 0) ? (inv.second + h) : (inv.second % h);
    desc.x = h * inv2;
    desc.y = w * inv3;
    ss << R"#(static __device__  inline unsigned inverse2(unsigned index, const unsigned digits) {
    index = (index << 16) | (index >> 16);
    index = ((index & 0x00ff00ff) << 8) | ((index & 0xff00ff00) >> 8);
    index = ((index & 0x0f0f0f0f) << 4) | ((index & 0xf0f0f0f0) >> 4);
    index = ((index & 0x33333333) << 2) | ((index & 0xcccccccc) >> 2);
    index = ((index & 0x55555555) << 1) | ((index & 0xaaaaaaaa) >> 1);
    return index >> (32 - digits);
}
static __device__ inline unsigned inverse3(unsigned index, const unsigned digits) {
    unsigned result = 0;
    for (unsigned d = 0; d < digits; ++d) {
        result = result * 3 + index % 3;
        index /= 3;
    }
    return result;
}

struct DataDesc final {
    unsigned p2, p3, x, y, increment;
    float scaleX, scaleY;
};

extern "C" __device__ SamplerInitResult __direct_callable__init(const unsigned i, const unsigned x, const unsigned y) {
    const DataDesc* data = getSBTData<DataDesc>();
    // Promote to 64 bits to avoid overflow.
    const unsigned long long hx = inverse2(x, data->p2);
    const unsigned long long hy = inverse3(y, data->p3);
    // Apply Chinese remainder theorem.
    const unsigned offset = static_cast<unsigned>((hx * data->x + hy * data->y) % data->increment);
    SamplerInitResult res;
    res.index = offset + i * data->increment;
    res.px = __direct_callable__sample2(res.index) * data->scaleX;
    res.py = __direct_callable__sample3(res.index) * data->scaleY;
    return res;
}
)#";
    return ~0u / desc.increment;
}

// TODO:ptx caching
class Halton final : public Sampler {
private:
    std::vector<ProgramGroup> mPrograms;

public:
    explicit Halton(Bus::ModuleInstance& instance) : Sampler(instance) {}
    SamplerData init(PluginHelper helper, std::shared_ptr<Config> config,
                     Uint2 size, unsigned maxDim) override {
        BUS_TRACE_BEG() {
            std::vector<unsigned> primeTable = getPrimeTable(maxDim + 2);
            std::stringstream ss;
            ss << "#include <KernelInclude.hpp>" << std::endl;
            generateSample(ss, maxDim + 2, config, reporter(), primeTable);
            DataDesc data;
            SamplerData res;
            res.maxSPP = generateInit(ss, size, data);
            reporter().apply(ReportLevel::Debug,
                             "maxSPP=" + std::to_string(res.maxSPP),
                             BUS_DEFSRCLOC());
            auto src = ss.str();
            /*
            reporter().apply(ReportLevel::Debug, "Source:\n" + src,
                             BUS_DEFSRCLOC());
                             */
            std::hash<std::string> hasher;
            const ModuleDesc& mod = helper->getModuleManager()->getModule(
                BUS_DEFAULT_MODULE_NAME + std::to_string(hasher(src)),
                [&] { return helper->getModuleManager()->compileSrc(src); });
            std::vector<OptixProgramGroupDesc> descs;
            // init
            {
                OptixProgramGroupDesc desc = {};
                desc.flags = 0;
                desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
                desc.callables.moduleDC = mod.handle.get();
                desc.callables.entryFunctionNameDC =
                    mod.map("__direct_callable__init");
                descs.emplace_back(desc);
            }
            for(unsigned i = 0; i < maxDim; ++i) {
                unsigned base = primeTable[i + 2];
                OptixProgramGroupDesc desc = {};
                desc.flags = 0;
                desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
                desc.callables.moduleDC = mod.handle.get();
                desc.callables.entryFunctionNameDC =
                    mod.map("__direct_callable__sample" + std::to_string(base));
                descs.emplace_back(desc);
            }
            std::vector<OptixProgramGroup> pgs(descs.size());
            OptixProgramGroupOptions opt = {};
            checkOptixError(
                optixProgramGroupCreate(helper->getContext(), descs.data(),
                                        static_cast<unsigned>(descs.size()),
                                        &opt, nullptr, nullptr, pgs.data()));
            for(auto prog : pgs)
                mPrograms.emplace_back(prog);
            for(auto prog : pgs) {
                if(prog == pgs.front())
                    res.sbtData.emplace_back(packSBTRecord(prog, data));
                else
                    res.sbtData.emplace_back(packEmptySBTRecord(prog));
            }
            res.group = pgs;
            res.dssInit = res.dssSample = 0;
            for(size_t i = 0; i < pgs.size(); ++i) {
                OptixStackSizes size;
                checkOptixError(optixProgramGroupGetStackSize(pgs[i], &size));
                unsigned& siz = (i ? res.dssSample : res.dssInit);
                siz = std::max(siz, size.dssDC);
            }
            return res;
        }
        BUS_TRACE_END();
    }
};

class Instance final : public Bus::ModuleInstance {
public:
    Instance(const fs::path& path, Bus::ModuleSystem& sys)
        : Bus::ModuleInstance(path, sys) {
        optixInit();
    }
    Bus::ModuleInfo info() const override {
        Bus::ModuleInfo res;
        res.name = "Piper.BuiltinSampler.Halton";
        res.guid = Bus::str2GUID("{2A6E792A-48CB-4E3F-8DC9-38D09B6B259F}");
        res.busVersion = BUS_VERSION;
        res.version = "0.0.1";
        res.description = " Halton Sequence Sampler";
        res.copyright = "Copyright (c) 2019 Zheng Yingwei";
        res.modulePath = getModulePath();
        return res;
    }
    std::vector<Bus::Name> list(Bus::Name api) const override {
        if(api == Sampler::getInterface())
            return { "Halton" };
        return {};
    }
    std::shared_ptr<Bus::ModuleFunctionBase> instantiate(Name name) override {
        if(name == "Halton")
            return std::make_shared<Halton>(*this);
        return nullptr;
    }
};

BUS_API void busInitModule(const Bus::fs::path& path, Bus::ModuleSystem& system,
                           std::shared_ptr<Bus::ModuleInstance>& instance) {
    instance = std::make_shared<Instance>(path, system);
}
