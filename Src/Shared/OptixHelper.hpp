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
        if(std::uncaught_exceptions() == 0)
            throw std::runtime_error(std::string("CudaError[") + name + "]" +
                                     string);
    }
}

inline void checkOptixError(OptixResult res) {
    if(res != OptixResult::OPTIX_SUCCESS) {
        if(std::uncaught_exceptions() == 0)
            throw std::runtime_error(std::string("OptixError[") +
                                     optixGetErrorName(res) + "]" +
                                     optixGetErrorString(res));
    }
}

struct DevPtrDeleter final {
    size_t offset;
    DevPtrDeleter() : offset(0) {}
    DevPtrDeleter(size_t off) : offset(off) {}
    void operator()(void* ptr) const {
        static_assert(sizeof(CUdeviceptr) == sizeof(void*));
        checkCudaError(cuMemFree(reinterpret_cast<CUdeviceptr>(ptr) - offset));
    }
};

using Buffer = std::unique_ptr<void, DevPtrDeleter>;

template <typename T>
inline T alignTo(T siz, T align) {
    if(siz == 0)
        return align;
    if(siz % align)
        return (siz / align + 1) * align;
    return siz;
}

inline Buffer allocBuffer(size_t siz, size_t align = 16) {
    align = std::max(align, 16ull);  // TODO:alignment
    CUdeviceptr ptr;
    checkCudaError(cuMemAlloc(&ptr, siz + align));
    CUdeviceptr uptr = (ptr / align + 1) * align;
    return Buffer{ reinterpret_cast<void*>(uptr), DevPtrDeleter{ uptr - ptr } };
}

inline CUdeviceptr asPtr(const Buffer& buf) {
    return reinterpret_cast<CUdeviceptr>(buf.get());
}

using Data = std::vector<std::byte>;
template <typename T>
Data packSBTRecord(OptixProgramGroup prog, const T& data) {
    Data res(OPTIX_SBT_RECORD_HEADER_SIZE + sizeof(data));
    checkOptixError(optixSbtRecordPackHeader(prog, res.data()));
    memcpy(res.data() + OPTIX_SBT_RECORD_HEADER_SIZE, &data, sizeof(data));
    return res;
}

inline Data packEmptySBTRecord(OptixProgramGroup prog) {
    Data res(OPTIX_SBT_RECORD_HEADER_SIZE);
    checkOptixError(optixSbtRecordPackHeader(prog, res.data()));
    return res;
}

template <typename T>
inline Buffer uploadParam(CUstream stream, const T& x, size_t align = 16) {
    Buffer res = allocBuffer(sizeof(T), align);
    checkCudaError(cuMemcpyHtoDAsync(asPtr(res), &x, sizeof(T), stream));
    return res;
}

template <typename T>
inline Buffer uploadData(CUstream stream, T* data, size_t size,
                         size_t align = 16) {
    Buffer res = allocBuffer(sizeof(T) * size, align);
    checkCudaError(
        cuMemcpyHtoDAsync(asPtr(res), data, sizeof(T) * size, stream));
    return res;
}

inline Buffer uploadData(CUstream stream, const Data& data, size_t align = 16) {
    Buffer res = allocBuffer(data.size(), align);
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

template <typename T>
inline T downloadData(const Buffer& buf, size_t offsetByte) {
    T res = {};
    checkCudaError(cuMemcpyDtoH(&res, asPtr(buf) + offsetByte, sizeof(T)));
    return res;
}

struct ProgramGroupDeleter final {
    void operator()(OptixProgramGroup mod) const {
        checkOptixError(optixProgramGroupDestroy(mod));
    }
};

using ProgramGroup = std::unique_ptr<OptixProgramGroup_t, ProgramGroupDeleter>;

struct PipelineDeleter final {
    void operator()(OptixPipeline pipe) const {
        checkOptixError(optixPipelineDestroy(pipe));
    }
};

using Pipeline = std::unique_ptr<OptixPipeline_t, PipelineDeleter>;

inline Buffer uploadSBTRecords(CUstream stream, const std::vector<Data>& data,
                               CUdeviceptr& ptr, unsigned& stride,
                               unsigned& count) {
    BUS_TRACE_BEGIN("Piper.Builtin.OptixHelper") {
        stride = 0;
        count = static_cast<unsigned>(data.size());
        for(auto&& d : data)
            stride = std::max(stride, static_cast<unsigned>(d.size()));
        stride = alignTo<unsigned>(stride, OPTIX_SBT_RECORD_ALIGNMENT);
        Data base(stride * count);
        for(unsigned i = 0; i < count; ++i) {
            memcpy(base.data() + i * stride, data[i].data(), data[i].size());
        }
        Buffer buf = allocBuffer(stride * count, OPTIX_SBT_RECORD_ALIGNMENT);
        ptr = asPtr(buf);
        checkCudaError(
            cuMemcpyHtoDAsync(ptr, base.data(), base.size(), stream));
        return buf;
    }
    BUS_TRACE_END();
}
