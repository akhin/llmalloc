#ifndef _HINTS_HOT_CODE_H_
#define _HINTS_HOT_CODE_H_

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FORCE_INLINE
#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline))
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ALIGN_DATA , some GCC versions gives warnings about standard C++ 'alignas' when applied to data
#ifdef __GNUC__
#define ALIGN_DATA( _alignment_ ) __attribute__((aligned( (_alignment_) )))
#elif _MSC_VER
#define ALIGN_DATA( _alignment_ ) alignas( _alignment_ )
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ALIGN_CODE, using alignas(64) or __attribute__(aligned(alignment)) for a function will work in GCC but MSVC won't compile
#ifdef __GNUC__
#define ALIGN_CODE( _alignment_ ) __attribute__((aligned( (_alignment_) )))
#elif _MSC_VER
//No implementation provided for MSVC :
#define ALIGN_CODE( _alignment_ )
#endif


#endif