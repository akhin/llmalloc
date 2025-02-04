#ifndef _HINTS_BRANCH_PREDICTOR_H_
#define _HINTS_BRANCH_PREDICTOR_H_

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LIKELY
#if defined(_MSC_VER)
//No implementation provided for MSVC for pre C++20 :
//https://social.msdn.microsoft.com/Forums/vstudio/en-US/2dbdca4d-c0c0-40a3-993b-dc78817be26e/branch-hints?forum=vclanguage
#define likely(x) x
#elif defined(__GNUC__)
#define likely(x)      __builtin_expect(!!(x), 1)
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UNLIKELY
#if defined(_MSC_VER)
//No implementation provided for MSVC for pre C++20 :
//https://social.msdn.microsoft.com/Forums/vstudio/en-US/2dbdca4d-c0c0-40a3-993b-dc78817be26e/branch-hints?forum=vclanguage
#define unlikely(x) x
#elif defined(__GNUC__)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#endif

#endif