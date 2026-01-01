#pragma once

//////////////////////////////////////////////////////////////////////
// COMPILER CHECK
#if (! defined(_MSC_VER)) && (! defined(__GNUC__))
#error "This library is supported for only GCC and MSVC compilers"
#endif

//////////////////////////////////////////////////////////////////////
// C++ VERSION CHECK
#if defined(_MSC_VER)
#if _MSVC_LANG < 201703L
#error "This library requires to be compiled with at least C++17"
#endif
#elif defined(__GNUC__)
#if __cplusplus < 201703L
#error "This library requires to be compiled with at least C++17"
#endif
#endif