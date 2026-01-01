#pragma once

#ifdef __linux__ // VOLTRON_EXCLUDE
#include <cstring>
#include <limits.h>
#include <dlfcn.h>
#include <errno.h>
#endif // VOLTRON_EXCLUDE

#include <atomic>
#include <stdexcept>
#include "compiler/unused.h"
#include "compiler/hints_hot_code.h"
#include "cpu/alignment_constants.h"

// VOLTRON_NAMESPACE_EXCLUSION_START
////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OVERRIDES
#ifdef ENABLE_OVERRIDE

#ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
static std::atomic<bool> g_scalable_malloc_initialised = false;

void llmalloc_initialise()
{
    if(g_scalable_malloc_initialised.load() == false)
    {
        auto success = llmalloc::ScalableMalloc::get_instance().create();

        if(success == false)
        {
            throw std::runtime_error("llmalloc initialisation failed");
        }

        g_scalable_malloc_initialised.store(true);
    }
}
#endif

// ALL REPLACEMENTS WILL BE DIRECTED TO FUNCTIONS BELOW
LLMALLOC_ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_malloc(std::size_t size)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().allocate(size);
}

LLMALLOC_ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_aligned_malloc(std::size_t size, std::size_t alignment)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().allocate_aligned(size, alignment);
}

LLMALLOC_ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_operator_new(std::size_t size)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().operator_new(size);
}

LLMALLOC_ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_operator_new_aligned(std::size_t size, std::size_t alignment)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().operator_new_aligned(size, alignment);
}

LLMALLOC_ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void llmalloc_free(void* ptr)
{
    return llmalloc::ScalableMalloc::get_instance().deallocate(ptr);
}

LLMALLOC_ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_calloc(std::size_t num, std::size_t size)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().allocate_and_zero_memory(num, size);
}

LLMALLOC_ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_realloc(void* ptr, std::size_t size)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().reallocate(ptr, size);
}

LLMALLOC_ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
std::size_t llmalloc_usable_size(void* ptr)
{
    return llmalloc::ScalableMalloc::get_instance().get_usable_size(ptr);
}

LLMALLOC_ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_aligned_realloc(void* ptr, std::size_t size, std::size_t alignment)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().aligned_reallocate(ptr, size, alignment);
}

#ifdef __linux__

struct mallinfo 
{
    int arena;    // total space allocated from the system
    int ordblks;  // number of non-inuse chunks
    int smblks;   // unused -- always zero
    int hblks;    // number of mmapped regions
    int hblkhd;   // total space in mmapped regions
    int usmblks;  // unused -- always zero
    int fsmblks;  // unused -- always zero
    int uordblks; // total allocated space
    int fordblks; // total non-inuse space
    int keepcost; // top-most, releasable space (in bytes)
};

mallinfo llmalloc_mallinfo()
{
    struct mallinfo info = {0};
    return info;
}

int llmalloc_mallopt(int param, int value)
{
    return 0;
}

int llmalloc_malloc_trim(std::size_t pad)
{
    // Does not support trimming, it returns 0 to indicate success.
    return 0;
}

void *llmalloc_valloc(std::size_t size)
{
    return llmalloc_aligned_malloc(size, llmalloc::VirtualMemory::get_page_size());
}

void *llmalloc_pvalloc(std::size_t size)
{
    auto page_size = llmalloc::VirtualMemory::get_page_size();
    return llmalloc_aligned_malloc(llmalloc::AlignmentAndSizeUtils::get_next_pow2_multiple_of(size,page_size), page_size);
}

int llmalloc_posix_memalign(void **memptr, std::size_t alignment, std::size_t size)
{
    if(llmalloc::AlignmentAndSizeUtils::is_pow2(alignment) == false)
    {
        return EINVAL;
    }

    if (memptr == nullptr) 
    {
        return EINVAL;
    }

    if( size == 0 )
    {
        *memptr = nullptr;
        return 0;
    }    

    auto ret = llmalloc_aligned_malloc(size, alignment);
    *memptr = ret;
    return (*memptr != nullptr) ? 0 : ENOMEM;
}

char *llmalloc_strdup(const char *s)
{
    std::size_t size = strlen(s);
    char *new_str = static_cast<char *>(llmalloc_malloc(size+1));

    if (new_str != nullptr)
    {
        llmalloc_builtin_memcpy(new_str, s, size);
        new_str[size] = '\0';
    }

    return new_str;
}

char *llmalloc_strndup(const char *s, size_t n)
{
    std::size_t size = strnlen(s, n);
    char *new_str = static_cast<char *>(llmalloc_malloc(size+1));

    if (new_str != nullptr)
    {
        llmalloc_builtin_memcpy(new_str, s, size);
        new_str[size] = '\0';
    }

    return new_str;
}

char *llmalloc_realpath(const char *path, char *resolved_path)
{
    char *(*original_realpath)(const char *, char *) =(char *(*)(const char *, char *))dlsym(RTLD_NEXT, "realpath"); // Will invoke also our malloc

    if(original_realpath==nullptr)
    {
        return nullptr;
    }

    if(resolved_path != nullptr)
    {
        return original_realpath(path, resolved_path);
    }

    char* buffer = static_cast<char*>(llmalloc_malloc(PATH_MAX));
    return original_realpath(path, buffer);
}

void* llmalloc_reallocarray(void *ptr, size_t nelem, size_t elsize)
{
    std::size_t total = nelem * elsize;
    int err = __builtin_umull_overflow(nelem, elsize, &total);

    if (err) 
    {
        errno = EINVAL;
        return nullptr;
    }

    return llmalloc_realloc(ptr, total);
}
#endif

#define malloc(size) llmalloc_malloc(size)
#define free(ptr) llmalloc_free(ptr)
#define calloc(num, size) llmalloc_calloc(num, size)
#define realloc(ptr, size) llmalloc_realloc(ptr, size)
#ifdef _WIN32
#define _aligned_malloc(size, alignment) llmalloc_aligned_malloc(size, alignment)
#define _aligned_free(ptr) llmalloc_free(ptr)
#define _msize(ptr) llmalloc_usable_size(ptr)
#define _aligned_realloc(ptr, size, alignment) llmalloc_aligned_realloc(ptr, size, alignment)
#endif
#ifdef __linux__
#define aligned_alloc(alignment, size) llmalloc_aligned_malloc(size, alignment)
#define malloc_usable_size(ptr) llmalloc_usable_size(ptr)
#define cfree(ptr) llmalloc_free(ptr)
#define memalign(alignment, size) llmalloc_aligned_malloc(size, alignment)
#define reallocf(ptr, size) llmalloc_realloc(ptr, size)
#define reallocarray(ptr, nelem, elsize) llmalloc_reallocarray(ptr, nelem,elsize)
#define mallinfo llmalloc_mallinfo
#define mallopt(param, value) llmalloc_mallopt(param, value)
#define malloc_trim(pad) llmalloc_malloc_trim(pad)
#define valloc(size) llmalloc_valloc(size)
#define pvalloc(size) llmalloc_pvalloc(size)
#define posix_memalign(memptr, alignment, size) llmalloc_posix_memalign(memptr, alignment, size)
#define strdup(s) llmalloc_strdup(s)
#define strndup(s, n) llmalloc_strndup(s, n)
#define realpath(path, resolved_path) llmalloc_realpath(path, resolved_path)
#endif

///////////////////////////////////////////////////////////////////////
// USUAL OVERLOADS
void* operator new(std::size_t size)
{
    return llmalloc_operator_new(size);
}

void operator delete(void* ptr)
{
    llmalloc_free(ptr);
}

void* operator new[](std::size_t size)
{
    return llmalloc_operator_new(size);
}

void operator delete[](void* ptr) noexcept
{
    llmalloc_free(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH std::nothrow_t
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return llmalloc_malloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    llmalloc_free(ptr);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return llmalloc_malloc(size);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    llmalloc_free(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return llmalloc_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    LLMALLOC_UNUSED(alignment);
    llmalloc_free(ptr);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return llmalloc_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    LLMALLOC_UNUSED(alignment);
    llmalloc_free(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT std::size_t
void* operator new(std::size_t size, std::size_t alignment)
{
    return llmalloc_operator_new_aligned(size, alignment);
}

void* operator new[](std::size_t size, std::size_t alignment)
{
    return llmalloc_operator_new_aligned(size, alignment);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT and std::nothrow_t
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    LLMALLOC_UNUSED(tag);
    return llmalloc_aligned_malloc(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    LLMALLOC_UNUSED(tag);
    return llmalloc_aligned_malloc(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    llmalloc_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    llmalloc_free(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT and std::nothrow_t & std::size_t alignment not std::align_val_t
void* operator new(std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
{
    LLMALLOC_UNUSED(tag);
    return llmalloc_aligned_malloc(size, alignment);
}

void* operator new[](std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
{
    LLMALLOC_UNUSED(tag);
    return llmalloc_aligned_malloc(size, alignment);
}

void operator delete(void* ptr, std::size_t, const std::nothrow_t &) noexcept
{
    llmalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t, const std::nothrow_t &) noexcept
{
    llmalloc_free(ptr);
}

///////////////////////////////////////////////////////////////////////
// DELETES WITH SIZES
void operator delete(void* ptr, std::size_t size) noexcept
{
    LLMALLOC_UNUSED(size);
    llmalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept
{
    LLMALLOC_UNUSED(size);
    llmalloc_free(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    LLMALLOC_UNUSED(size);
    LLMALLOC_UNUSED(align);
    llmalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    LLMALLOC_UNUSED(size);
    LLMALLOC_UNUSED(align);
    llmalloc_free(ptr);
}

void operator delete(void* ptr, std::size_t size, std::size_t align) noexcept
{
    LLMALLOC_UNUSED(size);
    LLMALLOC_UNUSED(align);
    llmalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::size_t align) noexcept
{
    LLMALLOC_UNUSED(size);
    LLMALLOC_UNUSED(align);
    llmalloc_free(ptr);
}

#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VOLTRON_NAMESPACE_EXCLUSION_END