#pragma once
#pragma warning(push, 0)
#include "../ThirdParty/Bus/BusCommon.hpp"
#include <cuda.h>
#include <optix.h>
#pragma warning(pop)

using Bus::Unmoveable;

inline void checkCudaError(CUresult err) {
    if(err != CUDA_SUCCESS) {
        const char *name, *string;
        cuGetErrorName(err, &name);
        cuGetErrorString(err, &string);
        if(name == nullptr || string == nullptr)
            name = string = "Unknown Error";
        throw std::runtime_error(std::string("CudaError[") + name + "]" +
                                 string);
    }
}

inline void checkOptixError(OptixResult res) {
    if(res != OptixResult::OPTIX_SUCCESS) {
        throw std::runtime_error(std::string("OptixError[") +
                                 optixGetErrorName(res) + "]" +
                                 optixGetErrorString(res));
    }
}

struct DevPtrDeleter final {
    void operator()(void* ptr) const {
        static_assert(sizeof(CUdeviceptr) == sizeof(void*));
        checkCudaError(cuMemFree(reinterpret_cast<CUdeviceptr>(ptr)));
    }
};

using Buffer = std::unique_ptr<void, DevPtrDeleter>;

inline Buffer allocBuffer(size_t siz) {
    CUdeviceptr ptr;
    checkCudaError(cuMemAlloc(&ptr, siz));
    return Buffer{ reinterpret_cast<void*>(ptr) };
}

inline CUdeviceptr asPtr(const Buffer& buf) {
    return reinterpret_cast<CUdeviceptr>(buf.get());
}

using Data = std::vector<std::byte>;
template <typename T>
Data packSBT(OptixProgramGroup prog, const T& data) {
    Data res(OPTIX_SBT_RECORD_HEADER_SIZE + sizeof(data));
    checkOptixError(optixSbtRecordPackHeader(prog, res.data()));
    memcpy(res.data() + OPTIX_SBT_RECORD_HEADER_SIZE, &data, sizeof(data));
    return res;
}

inline Data packEmptySBT(OptixProgramGroup prog) {
    Data res(OPTIX_SBT_RECORD_HEADER_SIZE);
    checkOptixError(optixSbtRecordPackHeader(prog, res.data()));
    return res;
}

template <typename T>
inline Buffer uploadParam(CUstream stream, const T& x) {
    Buffer res = allocBuffer(sizeof(T));
    checkCudaError(cuMemcpyHtoDAsync(asPtr(res), &x, sizeof(T), stream));
    return res;
}

template <typename T>
inline Buffer uploadData(CUstream stream, T* data, size_t size) {
    Buffer res = allocBuffer(sizeof(T) * size);
    checkCudaError(
        cuMemcpyHtoDAsync(asPtr(res), data, sizeof(T) * size, stream));
    return res;
}

inline Buffer uploadData(CUstream stream, const Data& data) {
    Buffer res = allocBuffer(data.size());
    checkCudaError(
        cuMemcpyHtoDAsync(asPtr(res), data.data(), data.size(), stream));
    return res;
}

template <typename T>
inline std::vector<T> downloadData(const Buffer& buf, size_t offsetByte,
                                   size_t size) {
    std::vector<T> res(size);
    checkCudaError(
        cuMemcpyDtoH(res.data(), asPtr(buf) + offsetByte, size * sizeof(T)));
    return res;
}

struct ModuleDeleter final {
    void operator()(OptixModule mod) const {
        checkOptixError(optixModuleDestroy(mod));
    }
};

using Module = std::unique_ptr<OptixModule_t, ModuleDeleter>;

struct ProgramGroupDeleter final {
    void operator()(OptixProgramGroup mod) const {
        checkOptixError(optixProgramGroupDestroy(mod));
    }
};

using ProgramGroup = std::unique_ptr<OptixProgramGroup_t, ProgramGroupDeleter>;
