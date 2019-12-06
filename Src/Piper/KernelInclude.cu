#pragma once
#define __int32 int
#define __int64 long long
#define _NODISCARD
#define WCHAR_MIN 0x0000
#define WCHAR_MAX 0xffff
#define CUDA_VERSION 10020
namespace std {
    typedef signed char int8_t;
    typedef short int16_t;
    typedef int int32_t;
    typedef long long int64_t;
    typedef unsigned char uint8_t;
    typedef unsigned short uint16_t;
    typedef unsigned int uint32_t;
    typedef unsigned long long uint64_t;
    typedef uint64_t size_t;
    template <typename genType>
    struct make_unsigned {};

    template <>
    struct make_unsigned<char> {
        typedef unsigned char type;
    };

    template <>
    struct make_unsigned<signed char> {
        typedef unsigned char type;
    };

    template <>
    struct make_unsigned<short> {
        typedef unsigned short type;
    };

    template <>
    struct make_unsigned<int> {
        typedef unsigned int type;
    };

    template <>
    struct make_unsigned<long> {
        typedef unsigned long type;
    };

    template <>
    struct make_unsigned<int64_t> {
        typedef uint64_t type;
    };

    template <>
    struct make_unsigned<unsigned char> {
        typedef unsigned char type;
    };

    template <>
    struct make_unsigned<unsigned short> {
        typedef unsigned short type;
    };

    template <>
    struct make_unsigned<unsigned int> {
        typedef unsigned int type;
    };

    template <>
    struct make_unsigned<unsigned long> {
        typedef unsigned long type;
    };

    template <>
    struct make_unsigned<uint64_t> {
        typedef uint64_t type;
    };

    using ::acos;
    using ::asin;
    using ::atan;
    using ::ceil;
    using ::cos;
    using ::cosh;
    using ::exp;
    using ::floor;
    using ::log;
    using ::pow;
    using ::sin;
    using ::sinh;
    using ::sqrt;
    using ::tan;
    using ::tanh;
}  // namespace std
constexpr long long operator"" i64(unsigned long long x) {
    return x;
}
constexpr unsigned long long operator"" ui64(unsigned long long x) {
    return x;
}
using std::uint32_t;
using std::uint64_t;
#define _HUGE_ENUF 1e+300  // _HUGE_ENUF*_HUGE_ENUF must overflow
#define INFINITY ((float)(_HUGE_ENUF * _HUGE_ENUF))
#define HUGE_VAL ((double)INFINITY)
#define HUGE_VALF ((float)INFINITY)
#define __builtin_huge_val()  HUGE_VAL
#define __builtin_huge_valf()  HUGE_VALF 
#define __builtin_nan nan
#define __builtin_nanf  nanf 
#define __builtin_nans nan
#define __builtin_nansf nanf
#include "../Shared/KernelShared.hpp"
