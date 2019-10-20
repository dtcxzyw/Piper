#include "../../Shared/SamplerAPI.hpp"
#include "../../Shared/ConfigAPI.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#pragma warning(push, 0)
#include <optix_function_table_definition.h>
#include <optix_stubs.h>
#pragma warning(pop)

// http://gruenschloss.org/halton/halton.zip
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
             unsigned base, unsigned maxTableSize, unsigned kth) {
    // Special case: radical inverse in base 2, with direct bit reversal.
    if(base == 2) {
        ss << R"#(extern "C" __device__ float __direct_callable__sample0(unsigned idx) {
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
           << kth << "(unsigned idx) {\n";
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

std::string generateSoruce(unsigned maxDim, std::shared_ptr<Config> config,
                           Bus::Reporter& reporter) {
    BUS_TRACE_BEG() {
        if(maxDim == 0) {
            reporter.apply(ReportLevel::Warning, "MaxDim==0", BUS_DEFSRCLOC());
            return "";
        }
        if(maxDim > 256)
            BUS_TRACE_THROW(std::runtime_error("Need MaxDim<=256"));
        std::vector<unsigned> primeTable = getPrimeTable(maxDim);
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
        std::stringstream ss;
        ss.precision(15);
        for(unsigned i = 0; i < maxDim; ++i) {
            unsigned base = primeTable[i];
            genCode(ss, perms[base], base, maxTableSize, i);
        }
        return ss.str();
    }
    BUS_TRACE_END();
}

class Halton final : public Sampler {
private:
    std::vector<ProgramGroup> mPrograms;

public:
    explicit Halton(Bus::ModuleInstance& instance) : Sampler(instance) {}
    SamplerData init(PluginHelper helper, std::shared_ptr<Config> config,
                     unsigned maxDim) override {
        BUS_TRACE_BEG() {
            std::string src = generateSoruce(maxDim, config, reporter());
            reporter().apply(ReportLevel::Debug, "Source:\n" + src,
                             BUS_DEFSRCLOC());
            OptixModule mod = helper->loadModuleFromSrc(src);
            std::vector<std::string> funcNames;
            std::vector<OptixProgramGroupDesc> descs;
            for(unsigned i = 0; i < maxDim; ++i) {
                funcNames.emplace_back("__direct_callable__sample" +
                                       std::to_string(i));
                OptixProgramGroupDesc desc = {};
                desc.flags = 0;
                desc.kind = OPTIX_PROGRAM_GROUP_KIND_CALLABLES;
                desc.callables.moduleDC = mod;
                desc.callables.entryFunctionNameDC = funcNames.back().c_str();
                descs.emplace_back(desc);
            }
            std::vector<OptixProgramGroup> pgs(maxDim);
            OptixProgramGroupOptions opt = {};
            checkOptixError(optixProgramGroupCreate(
                helper->getContext(), descs.data(), maxDim, &opt, nullptr,
                nullptr, pgs.data()));
            for(auto prog : pgs)
                mPrograms.emplace_back(prog);
            SamplerData res;
            for(auto&& prog : mPrograms)
                res.sbtData.emplace_back(packEmptySBTRecord(prog.get()));
            res.group = pgs;
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
