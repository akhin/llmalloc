#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>
#include <limits.h>
#include <dlfcn.h>
#include <errno.h>

#include <llmalloc.h>
using namespace llmalloc;
using ScalableAllocatorType = ScalableMalloc;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SHARED OBJECT INITIALISATION
void initialise_shared_object() __attribute__((constructor));
void uninit_shared_object() __attribute__((destructor));

static bool shared_object_loaded = false;
static UserspaceSpinlock<> initialisatin_lock;

void initialise_shared_object()
{
    if(shared_object_loaded==true) return;

    initialisatin_lock.lock();
    // double checking

    if(shared_object_loaded==true) { initialisatin_lock.unlock(); return; }

    bool success = ScalableAllocatorType::get_instance().create();

    if(success == false)
    {
        fprintf(stderr, "llmalloc initialisation failed\n");
        std::terminate();
    }
    else
    {
        shared_object_loaded = true;
    }
    initialisatin_lock.unlock();
}

void uninit_shared_object()
{
}

extern "C"
{
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// malloc
void *malloc(std::size_t size)
{
    initialise_shared_object();
    return ScalableAllocatorType::get_instance().allocate(size);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// free
void free(void* ptr)
{
    initialise_shared_object();
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// calloc
void *calloc(std::size_t num, std::size_t size)
{
    initialise_shared_object();
    return ScalableAllocatorType::get_instance().allocate_and_zero_memory(num, size);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// realloc
void *realloc(void *ptr, std::size_t size)
{
    initialise_shared_object();
    return  ScalableAllocatorType::get_instance().reallocate(ptr, size);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// aligned_alloc
void *aligned_alloc(std::size_t alignment, std::size_t size)
{
    initialise_shared_object();
    return ScalableAllocatorType::get_instance().allocate_aligned(size, alignment);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// malloc_usable_size
std::size_t malloc_usable_size(void* ptr)
{
    initialise_shared_object();
    return ScalableAllocatorType::get_instance().get_usable_size(ptr);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// strdup
char *strdup(const char *s)
{
    initialise_shared_object();

    std::size_t size = strlen(s);
    char *new_str = static_cast<char *>(ScalableAllocatorType::get_instance().allocate(size+1));

    if (new_str != nullptr)
    {
        llmalloc_builtin_memcpy(new_str, s, size);
        new_str[size] = '\0';
    }

    return new_str;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// strndup
char *strndup(const char *s, size_t n)
{
    initialise_shared_object();

    std::size_t size = strnlen(s, n);
    char *new_str = static_cast<char *>(ScalableAllocatorType::get_instance().allocate(size+1));

    if (new_str != nullptr)
    {
        llmalloc_builtin_memcpy(new_str, s, size);
        new_str[size] = '\0';
    }

    return new_str;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// realpath
char *realpath(const char *path, char *resolved_path)
{
    initialise_shared_object();

    char *(*original_realpath)(const char *, char *) =(char *(*)(const char *, char *))dlsym(RTLD_NEXT, "realpath"); // Will invoke also our malloc
    
    if(original_realpath==nullptr)
    {
        return nullptr;
    }

    if(resolved_path != nullptr)
    {
        return original_realpath(path, resolved_path);
    }

    char* buffer = static_cast<char*>(ScalableAllocatorType::get_instance().allocate(PATH_MAX));
    return original_realpath(path, buffer);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// posix_memalign
int posix_memalign(void **memptr, std::size_t alignment, std::size_t size)
{
    initialise_shared_object();

    if( AlignmentAndSizeUtils::is_pow2(alignment) == false)
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

    auto ret = ScalableAllocatorType::get_instance().allocate_aligned(size, alignment);

    *memptr = ret;

    return (*memptr != nullptr) ? 0 : ENOMEM;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// memalign
void* memalign(std::size_t alignment, std::size_t size)
{
    initialise_shared_object();
    return ScalableAllocatorType::get_instance().allocate_aligned(size, alignment);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// reallocarray
void *reallocarray(void *ptr, std::size_t nelem, std::size_t elsize)
{
    initialise_shared_object();

    std::size_t total = nelem * elsize;
    int err = __builtin_umull_overflow(nelem, elsize, &total);

    if (err) 
    {
        errno = EINVAL;
        return nullptr;
    }

    return  ScalableAllocatorType::get_instance().reallocate(ptr, total);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LibC
#ifdef REDIRECT_GNU_LIBC

std::size_t __libc_malloc_usable_size(void *ptr)
{
    initialise_shared_object();

    return ScalableAllocatorType::get_instance().get_usable_size(ptr);
}

void* __libc_malloc(std::size_t size)
{
    initialise_shared_object();

    return ScalableAllocatorType::get_instance().allocate(size);
}

void* __libc_realloc(void* ptr, std::size_t size)
{  
    initialise_shared_object();

    return  ScalableAllocatorType::get_instance().reallocate(ptr, size);
}

void* __libc_calloc(std::size_t num, std::size_t size)
{
    initialise_shared_object();

    return  ScalableAllocatorType::get_instance().allocate_and_zero_memory(num, size);
}

void __libc_free(void* ptr)
{
    initialise_shared_object();

    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void __libc_cfree(void* ptr)
{
    initialise_shared_object();

    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void* __libc_memalign(std::size_t alignment, std::size_t size)
{
    initialise_shared_object();

    return ScalableAllocatorType::get_instance().allocate_aligned(size, alignment);
}

void* __libc_pvalloc(std::size_t size)
{
    initialise_shared_object();
    auto page_size = VirtualMemory::get_page_size();
    return ScalableAllocatorType::get_instance().allocate_aligned(AlignmentAndSizeUtils::get_next_pow2_multiple_of(size,page_size), page_size);
}

void* __libc_valloc(std::size_t size)
{
    initialise_shared_object();

    return ScalableAllocatorType::get_instance().allocate_aligned(size, VirtualMemory::get_page_size());
}

void __libc_free_sized(void* ptr, std::size_t size)
{
    initialise_shared_object();

    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void __libc_free_aligned_sized(void* ptr, std::size_t alignment, std::size_t size)
{
    initialise_shared_object();

    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void* __libc_reallocarray(void *ptr, size_t nelem, size_t elsize)
{    
    initialise_shared_object();

    std::size_t total = nelem * elsize;
    int err = __builtin_umull_overflow(nelem, elsize, &total);

    if (err) 
    {
        errno = EINVAL;
        return nullptr;
    }

    return ScalableAllocatorType::get_instance().reallocate(ptr, total);
}

#endif
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// cfree
void cfree(void *ptr)
{
    initialise_shared_object();

    ScalableAllocatorType::get_instance().deallocate(ptr);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// reallocf
void *reallocf(void *ptr, std::size_t size)
{
    initialise_shared_object();

    return ScalableAllocatorType::get_instance().reallocate(ptr, size);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// valloc
void *valloc(std::size_t size)
{
    initialise_shared_object();
    return ScalableAllocatorType::get_instance().allocate_aligned(size, VirtualMemory::get_page_size());
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// pvalloc
void *pvalloc(std::size_t size)
{
    initialise_shared_object();

    auto page_size = VirtualMemory::get_page_size();
    return ScalableAllocatorType::get_instance().allocate_aligned(AlignmentAndSizeUtils::get_next_pow2_multiple_of(size,page_size), page_size);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// malloc_trim
int malloc_trim(std::size_t pad)
{
    initialise_shared_object();

    // Since ScalableAllocator does not support trimming, it returns 0 to indicate success.
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// mallinfo

// From Linux die
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

mallinfo mallinfo()
{
    struct mallinfo info = {0};
    return info;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// mallopt
int mallopt(int param, int value)
{    
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
} // extern "C"

///////////////////////////////////////////////////////////////////////
// USUAL OVERLOADS
void* operator new(std::size_t size)
{
    initialise_shared_object();

    return ScalableAllocatorType::get_instance().operator_new(size);
}

void operator delete(void* ptr)
{
    initialise_shared_object();
    
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void* operator new[](std::size_t size)
{
    initialise_shared_object();
    
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void operator delete[](void* ptr) noexcept
{
    initialise_shared_object();
    
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH std::nothrow_t
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    initialise_shared_object();
    
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    initialise_shared_object();

    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    initialise_shared_object();
    
    return ScalableAllocatorType::get_instance().operator_new(size);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    initialise_shared_object();

    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT
void* operator new(std::size_t size, std::align_val_t alignment)
{
    initialise_shared_object();
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    initialise_shared_object();
    LLMALLOC_UNUSED(alignment);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    initialise_shared_object();   
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    initialise_shared_object();
    LLMALLOC_UNUSED(alignment);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT std::size_t
void* operator new(std::size_t size, std::size_t alignment)
{
    initialise_shared_object();

    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}


void* operator new[](std::size_t size, std::size_t alignment)
{
    initialise_shared_object();
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT and std::nothrow_t

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    initialise_shared_object();
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    initialise_shared_object();
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    initialise_shared_object();
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    initialise_shared_object();
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// WITH ALIGNMENT and std::nothrow_t   STD::SIZE_T not std::align_val_t

void* operator new(std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
{
    initialise_shared_object();
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
{
    initialise_shared_object();
    
    return ScalableAllocatorType::get_instance().operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr, std::size_t, const std::nothrow_t &) noexcept
{
    initialise_shared_object();

    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t, const std::nothrow_t &) noexcept
{
    initialise_shared_object();

    ScalableAllocatorType::get_instance().deallocate(ptr);
}

///////////////////////////////////////////////////////////////////////
// DELETES WITH SIZES
void operator delete(void* ptr, std::size_t size) noexcept
{
    initialise_shared_object();
    LLMALLOC_UNUSED(size);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept
{
    initialise_shared_object();
    LLMALLOC_UNUSED(size);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    initialise_shared_object();
    LLMALLOC_UNUSED(size);
    LLMALLOC_UNUSED(align);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    initialise_shared_object();
    LLMALLOC_UNUSED(size);
    LLMALLOC_UNUSED(align);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete(void* ptr, std::size_t size, std::size_t align) noexcept
{
    initialise_shared_object();
    LLMALLOC_UNUSED(size);
    LLMALLOC_UNUSED(align);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::size_t align) noexcept
{
    initialise_shared_object();
    LLMALLOC_UNUSED(size);
    LLMALLOC_UNUSED(align);
    ScalableAllocatorType::get_instance().deallocate(ptr);
}