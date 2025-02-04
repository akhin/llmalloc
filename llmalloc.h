/*
LLMALLOC VERSION 1.0.0

MIT License

Copyright (c) 2025 Akin Ocal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Acknowledgements:

This project uses a cosmetically modified version of Erik Rigtorp's lock-free queue implementation,
available at https://github.com/rigtorp/MPMCQueue, which is licenced under the MIT License.
*/
#ifndef _LLMALLOC_H_
#define _LLMALLOC_H_

// STD C
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cassert>
#include <cstring>
#include <cctype>
#include <cstdlib>
// STD
#include <type_traits>
#include <array>
#include <string_view>
#include <new>
#ifdef ENABLE_PMR
#include <memory_resource>
#endif
#ifdef ENABLE_OVERRIDE
#include <stdexcept>
#endif
// CPU INTRINSICS
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__)
#include <emmintrin.h>
#endif
// LINUX
#ifdef __linux__
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sched.h>
#include <fcntl.h>
#ifdef ENABLE_OVERRIDE
#include <limits.h>
#include <dlfcn.h>
#include <errno.h>
#endif
#ifdef ENABLE_NUMA
#include <numa.h>
#include <numaif.h>
#endif
#endif
// WINDOWS
#ifdef _WIN32
#include <windows.h>
#include <fibersapi.h>
#include <chrono>
#include <thread>
#endif
// TRACES
#if defined(ENABLE_PERF_TRACES) || defined(DISPLAY_ENV_VARS) || !defined(NDEBUG)
#include <cstdio>
#endif
// UNIT TESTS
#ifdef UNIT_TEST
#include <string>
#include <cstdio>
#endif

namespace llmalloc
{

//////////////////////////////////////////////////////////////////////
// COMPILER CHECK
#if (! defined(_MSC_VER)) && (! defined(__GNUC__))
#error "This library is supported for only GCC and MSVC compilers"
#endif

//////////////////////////////////////////////////////////////////////
// C++ VERSION CHECK
#if defined(_MSC_VER)
#if _MSVC_LANG < 201703L
#error "This library requires to be compiled with C++17"
#endif
#elif defined(__GNUC__)
#if __cplusplus < 201703L
#error "This library requires to be compiled with C++17"
#endif
#endif

//////////////////////////////////////////////////////////////////////
// ARCHITECTURE CHECK

#if defined(_MSC_VER)
#if (! defined(_M_X64))
#error "This library is supported for only x86-x64 architectures"
#endif
#elif defined(__GNUC__)
#if (! defined(__x86_64__)) && (! defined(__x86_64))
#error "This library is supported for only x86-x64 architectures"
#endif
#endif

//////////////////////////////////////////////////////////////////////
// OPERATING SYSTEM CHECK

#if (! defined(__linux__)) && (! defined(_WIN32) )
#error "This library is supported for Linux and Windows systems"
#endif

//////////////////////////////////////////////////////////////////////
// Count leading zeroes
#if defined(__GNUC__)
#define builtin_clzl(n)     __builtin_clzl(n)
#elif defined(_MSC_VER)
#if defined(_WIN64)    // Implementation is for 64-bit only.
inline int builtin_clzl(unsigned long value)
{
    unsigned long index = 0;
    return _BitScanReverse64(&index, static_cast<unsigned __int64>(value)) ? static_cast<int>(63 - index) : 64;
}
#else
#error "This code is intended for 64-bit Windows platforms only."
#endif
#endif

//////////////////////////////////////////////////////////////////////
// Compare and swap, standard C++ provides them however it requires non-POD std::atomic usage
// They are needed when we want to embed spinlocks in "packed" data structures which need all members to be POD such as headers
#if defined(__GNUC__)
#define builtin_cas(pointer, old_value, new_value) __sync_val_compare_and_swap(pointer, old_value, new_value)
#elif defined(_MSC_VER)
#define builtin_cas(pointer, old_value, new_value) _InterlockedCompareExchange(reinterpret_cast<long*>(pointer), new_value, old_value)
#endif

//////////////////////////////////////////////////////////////////////
// memcpy
#if defined(__GNUC__)
#define builtin_memcpy(destination, source, size)     __builtin_memcpy(destination, source, size)
#elif defined(_MSC_VER)
#define builtin_memcpy(destination, source, size)     std::memcpy(destination, source, size)
#endif

//////////////////////////////////////////////////////////////////////
// memset
#if defined(__GNUC__)
#define builtin_memset(destination, character, count)  __builtin_memset(destination, character, count)
#elif defined(_MSC_VER)
#define builtin_memset(destination, character, count)  std::memset(destination, character, count)
#endif

//////////////////////////////////////////////////////////////////////
// aligned_alloc , It exists because MSVC does not provide std::aligned_alloc
#if defined(__GNUC__)
#define builtin_aligned_alloc(size, alignment)  std::aligned_alloc(alignment, size)
#define builtin_aligned_free(ptr)               std::free(ptr)
#elif defined(_MSC_VER)
#define builtin_aligned_alloc(size, alignment)  _aligned_malloc(size, alignment)
#define builtin_aligned_free(ptr)               _aligned_free(ptr)
#endif

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

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PACKED

// Compilers may add additional padding zeroes for alignment
// Though those additions may increase the size of your structs/classes
// The ideal way is manually aligning data structures and minimising the memory footprint
// Compilers won`t add additional padding zeroes for "packed" data structures

#ifdef __GNUC__
#define PACKED( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#elif _MSC_VER
#define PACKED( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UNUSED
//To avoid unused variable warnings
#if defined(__GNUC__)
#define UNUSED(x) (void)(x)
#elif defined(_MSC_VER)
#define UNUSED(x) __pragma(warning(suppress:4100)) x
#endif

namespace AlignmentConstants
{
    // All constants are in bytes
    constexpr std::size_t CPU_CACHE_LINE_SIZE = 64;
    // SIMD REGISTER WIDTHS
    constexpr std::size_t SIMD_SSE42_WIDTH = 16;
    constexpr std::size_t SIMD_AVX_WIDTH = 32;
    constexpr std::size_t SIMD_AVX2_WIDTH = 32;
    constexpr std::size_t SIMD_AVX512_WIDTH = 64;
    constexpr std::size_t MINIMUM_VECTORISATION_WIDTH = SIMD_SSE42_WIDTH;
    constexpr std::size_t LARGEST_VECTORISATION_WIDTH = SIMD_AVX512_WIDTH; // AVX10 not available yet
    // VIRTUAL MEMORY PAGE SIZES ARE HANDLED IN os/virtual_memory.h
}

/*
    Intel initially advised using _mm_pause in spin-wait loops in case of hyperthreading
    Before Skylake it was about 10 cycles, but with Skylake it becomes 140 cycles and that applies to successor architectures
    -> Intel opt manual 2.5.4 "Pause Latency in Skylake Client Microarchitecture"

    Later _tpause  / _umonitor / _umwait instructions were introduced however not using them for the time being as they are not widespread yet

    Pause implementation is instead using nop
*/

inline void pause(uint16_t repeat_count=100)
{
    #if defined(__GNUC__)
    // rep is for repeating by the no provided in 16 bit cx register
    __asm__ __volatile__("mov %0, %%cx\n\trep; nop" : : "r" (repeat_count) : "cx");
    #elif defined(_WIN32)
    for (uint16_t i = 0; i < repeat_count; ++i)
    {
        _mm_lfence();
        __nop();
        _mm_lfence();
    }
    #endif
}

// ANSI coloured output for Linux , message box for Windows

#ifdef NDEBUG
#define assert_msg(expr, message) ((void)0)
#else
#ifdef __linux__
#define MAKE_RED(x)    "\033[0;31m" x "\033[0m"
#define MAKE_YELLOW(x) "\033[0;33m" x "\033[0m"
#define assert_msg(expr, message) \
            do { \
                if (!(expr)) { \
                    fprintf(stderr,  MAKE_RED("Assertion failed : ")  MAKE_YELLOW("%s") "\n", message); \
                    assert(false); \
                } \
            } while (0)
#elif _WIN32
#pragma comment(lib, "user32.lib")
#define assert_msg(expr, message) \
            do { \
                if (!(expr)) { \
                    MessageBoxA(NULL, message, "Assertion Failed", MB_ICONERROR | MB_OK); \
                    assert(false); \
                } \
            } while (0)
#endif
#endif

/*
    - To work with 2MB huge pages on Linux and  2MB or 1 GB huge pages on Windows , you may need to configure your system :

        - Linux : /proc/meminfo should have non-zero "Hugepagesize" & "HugePages_Total/HugePages_Free" attributes
                  ( If HugePages_Total or HugePages_Free  is 0
                  then run "echo 20 | sudo tee /proc/sys/vm/nr_hugepages" ( Allocates 20 x 2MB huge pages )
                  Reference : https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt )

                  ( If THP is enabled , we will use madvise. Otherwise we will use HUGE_TLB flag for mmap.
                  To check if THP enabled : cat /sys/kernel/mm/transparent_hugepage/enabled
                  To disable THP :  echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
                  )

        - Windows : SeLockMemoryPrivilege is required.
                    It can be acquired using gpedit.msc :
                    Local Computer Policy -> Computer Configuration -> Windows Settings -> Security Settings -> Local Policies -> User Rights Managements -> Lock pages in memory

    - For NUMA-local allocations :

            You need : #define ENABLE_NUMA

            Also if on Linux , you need libnuma ( For ex : RHEL -> sudo yum install numactl-devel & Ubuntu -> sudo apt install libnuma-dev ) and -lnuma for GCC
*/

#ifdef _WIN32
#pragma warning(disable:6250)
#endif

class VirtualMemory
{
    public:

        #ifdef __linux__
        constexpr static std::size_t PAGE_ALLOCATION_GRANULARITY = 4096;    // In bytes
        #elif _WIN32
        constexpr static std::size_t PAGE_ALLOCATION_GRANULARITY = 65536;   // In bytes , https://devblogs.microsoft.com/oldnewthing/20031008-00/?p=42223
        #endif

        static std::size_t get_page_size()
        {
            std::size_t ret{ 0 };

            #ifdef __linux__
            ret = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));        // TYPICALLY 4096, 2^ 12
            #elif _WIN32
            // https://learn.microsoft.com/en-gb/windows/win32/api/sysinfoapi/ns-sysinfoapi-system_info
            SYSTEM_INFO system_info;
            GetSystemInfo(&system_info);
            ret = system_info.dwPageSize; // TYPICALLY 4096, 2^ 12
            #endif
            return ret;
        }

        static bool is_huge_page_available()
        {
            bool ret{ false };
            #ifdef __linux__
            if (get_minimum_huge_page_size() <= 0)
            {
                ret = false;
            }
            else
            {
                if ( get_huge_page_total_count_2mb() > 0 )
                {
                    ret = true;
                }
            }
            #elif _WIN32
            auto huge_page_size = get_minimum_huge_page_size();

            if (huge_page_size)
            {
                HANDLE token = 0;
                OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token);

                if (token)
                {
                    LUID luid;

                    if (LookupPrivilegeValue(0, SE_LOCK_MEMORY_NAME, &luid))
                    {
                        TOKEN_PRIVILEGES token_privileges;
                        memset(&token_privileges, 0, sizeof(token_privileges));
                        token_privileges.PrivilegeCount = 1;
                        token_privileges.Privileges[0].Luid = luid;
                        token_privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

                        if (AdjustTokenPrivileges(token, FALSE, &token_privileges, 0, 0, 0))
                        {
                            auto last_error = GetLastError();

                            if (last_error  == ERROR_SUCCESS)
                            {
                                ret = true;
                            }
                        }
                    }
                }
            }
            #endif
            return ret;
        }

        static std::size_t get_minimum_huge_page_size()
        {
            std::size_t ret{ 0 };
            #ifdef __linux__
            ret = get_proc_mem_info("Hugepagesize", 13) * 1024; // It is in KBs
            #elif _WIN32
            ret = static_cast<std::size_t>(GetLargePageMinimum());
            #endif
            return ret;
        }

        // Note about alignments : Windows always returns page ( typically 4KB ) or huge page ( typially 2MB ) aligned addresses
        //                           On Linux , page sized ( again 4KB) allocations are aligned to 4KB, but the same does not apply to huge page allocations : They are aligned to 4KB but never to 2MB
        //                           Therefore in case of huge page use, there is no guarantee that the allocated address will be huge-page-aligned , so alignment requirements have to be handled by the caller
        //
        // Note about huge page failures : If huge page allocation fails, for the time being not doing a fallback for a subsequent non huge page allocation
        //                                    So library users have to check return values
        //
        static void* allocate(std::size_t size, bool use_huge_pages, int numa_node = -1, void* hint_address = nullptr)
        {
            void* ret = nullptr;
            #ifdef __linux__
            static bool thp_enabled = is_thp_enabled();
            // MAP_ANONYMOUS rather than going thru a file (memory mapped file)
            // MAP_PRIVATE rather than shared memory
            int flags = MAP_PRIVATE | MAP_ANONYMOUS;

            // MAP_POPULATE forces system to access the  just-allocated memory. That helps by creating TLB entries
            flags |= MAP_POPULATE;

            if(use_huge_pages)
            {
                if (!thp_enabled)
                {
                    flags |= MAP_HUGETLB;
                }
            }

            ret = mmap(hint_address, size, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (ret == nullptr || ret == MAP_FAILED)
            {
                return nullptr;
            }

            if(use_huge_pages)
            {
                if (thp_enabled)
                {
                    madvise(ret, size, MADV_HUGEPAGE);
                }
            }

            #ifdef ENABLE_NUMA
            if(numa_node >= 0)
            {
                auto numa_node_count = get_numa_node_count();

                if (numa_node_count > 0 && numa_node != static_cast<std::size_t>(-1))
                {
                    unsigned long nodemask = 1UL << numa_node;
                    int result = mbind(ret, size, MPOL_BIND, &nodemask, sizeof(nodemask), MPOL_MF_MOVE);

                    if (result != 0)
                    {
                        munmap(ret, size);
                        ret = nullptr;
                    }
                    else
                    {
                        int actual_numa_node = get_numa_node_of_address(ret);

                        if(actual_numa_node != numa_node)
                        {
                            munmap(ret, size);
                            ret = nullptr;
                        }
                    }
                }
            }
            #else
            UNUSED(numa_node);
            #endif

            #elif _WIN32
            int flags = MEM_RESERVE | MEM_COMMIT;

            if (use_huge_pages)
            {
                flags |= MEM_LARGE_PAGES;
            }

            #ifndef ENABLE_NUMA
            UNUSED(numa_node);
            ret = VirtualAlloc(hint_address, size, flags, PAGE_READWRITE);
            #else
            if(numa_node >= 0)
            {
                auto numa_node_count = get_numa_node_count();

                if (numa_node_count > 0 && numa_node != static_cast<std::size_t>(-1))
                {
                    ret = VirtualAllocExNuma(GetCurrentProcess(), hint_address, size, flags, PAGE_READWRITE, static_cast<DWORD>(numa_node));
                }
                else
                {
                    ret = VirtualAlloc(hint_address, size, flags, PAGE_READWRITE);
                }
            }
            else
            {
                ret = VirtualAlloc(hint_address, size, flags, PAGE_READWRITE);
            }
            #endif
            #endif

            return ret;
        }

        static bool deallocate(void* address, std::size_t size)
        {
            bool ret{ false };
            #ifdef __linux__
            ret = munmap(address, size) == 0 ? true : false;
            #elif _WIN32
            ret = VirtualFree(address, size, MEM_DECOMMIT) ? true : false;
            #endif
            return ret;
        }
        
        #ifdef __linux__
        // THP stands for "transparent huge page". A Linux mechanism
        // It affects how we handle allocation of huge pages on Linux
        static bool is_thp_enabled()
        {
            const char* thp_enabled_file = "/sys/kernel/mm/transparent_hugepage/enabled";
            
            if (access(thp_enabled_file, F_OK) != 0)
            {
                return false;
            }

            int fd = open(thp_enabled_file, O_RDONLY);
            if (fd < 0)
            {
                return false;
            }

            char buffer[256] = {0};
            ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
            close(fd);

            if (bytes_read <= 0)
            {
                return false;
            }

            if (strstr(buffer, "[always]") != nullptr || strstr(buffer, "[madvise]") != nullptr)
            {
                return true;
            }

            return false;
        }
        #endif

    private :
    
        #ifdef __linux__
        // Equivalent of /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
        static std::size_t get_huge_page_total_count_2mb()
        {
            auto ret = get_proc_mem_info("HugePages_Total", 16);

            if(ret == 0 )
            {
                ret = get_proc_mem_info("HugePages_Free", 15);
            }

            return ret;
        }

        static std::size_t get_proc_mem_info(const char* attribute, std::size_t attribute_len)
        {
            // Using syscalls to avoid memory allocations
            std::size_t ret = 0;
            const char* mem_info_file = "/proc/meminfo";

            int fd = open(mem_info_file, O_RDONLY);
            
            if (fd < 0) 
            {
                return ret;
            }

            char buffer[256] = {0};
            std::size_t read_bytes;

            while ((read_bytes = read(fd, buffer, sizeof(buffer))) > 0)
            {
                char* pos = strstr(buffer, attribute);

                if (pos != nullptr)
                {
                    ret = std::strtoul(pos + attribute_len, nullptr, 10);
                    break;
                }
            }

            close(fd);

            return ret;
        }
        #endif

        #ifdef ENABLE_NUMA
        static std::size_t get_numa_node_count()
        {
            std::size_t ret{ 0 };

            #ifdef __linux__
            // Requires -lnuma
            ret = static_cast<std::size_t>(numa_num_configured_nodes());
            #elif _WIN32
            // GetNumaHighestNodeNumber is not guaranteed to be equal to NUMA node count so we need to iterate backwards
            ULONG current_numa_node = 0;
            GetNumaHighestNodeNumber(&current_numa_node);

            while (current_numa_node > 0)
            {
                GROUP_AFFINITY affinity;
                if ((GetNumaNodeProcessorMaskEx)(static_cast<USHORT>(current_numa_node), &affinity))
                {
                    //If the specified node has no processors configured, the Mask member is zero
                    if (affinity.Mask != 0)
                    {
                        ret++;
                    }
                }
                // max node was invalid or had no processor assigned, try again
                current_numa_node--;
            }
            #endif

            return ret;
        }

        static int get_numa_node_of_address(void* ptr)
        {
            int actual_numa_node = -1;
            #ifdef __linux__
            get_mempolicy(&actual_numa_node, nullptr, 0, (void*)ptr, MPOL_F_NODE | MPOL_F_ADDR);
            #elif _WIN32
            // Not supported on Windows
            #endif
            return actual_numa_node;
        }
        #endif
};

/*  
    Standard C++ thread_local keyword does not allow you to specify thread specific destructors
    and also can't be applied to class members
*/

class ThreadLocalStorage
{
    public:

        static ThreadLocalStorage& get_instance()
        {
            static ThreadLocalStorage instance;
            return instance;
        }

        // Call it only once for a process
        bool create(void(*thread_destructor)(void*) = nullptr)
        {
            #if __linux__
            return pthread_key_create(&m_tls_index, thread_destructor) == 0;
            #elif _WIN32
            // Using FLSs rather TLS as it is identical + TLSAlloc doesn't support dtor
            m_tls_index = FlsAlloc(thread_destructor);
            return m_tls_index == FLS_OUT_OF_INDEXES ? false : true;
            #endif
        }

        // Same as create
        void destroy()
        {
            if (m_tls_index)
            {
                #if __linux__
                pthread_key_delete(m_tls_index);
                #elif _WIN32
                FlsFree(m_tls_index);
                #endif

                m_tls_index = 0;
            }
        }

        // GUARANTEED TO BE THREAD-SAFE/LOCAL
        void* get()
        {
            #if __linux__
            return pthread_getspecific(m_tls_index);
            #elif _WIN32
            return FlsGetValue(m_tls_index);
            #endif
        }

        void set(void* data_address)
        {
            #if __linux__
            pthread_setspecific(m_tls_index, data_address);
            #elif _WIN32
            FlsSetValue(m_tls_index, data_address);
            #endif
        }

    private:
            #if __linux__
            pthread_key_t m_tls_index = 0;
            #elif _WIN32
            unsigned long m_tls_index = 0;
            #endif

            ThreadLocalStorage() = default;
            ~ThreadLocalStorage() = default;

            ThreadLocalStorage(const ThreadLocalStorage& other) = delete;
            ThreadLocalStorage& operator= (const ThreadLocalStorage& other) = delete;
            ThreadLocalStorage(ThreadLocalStorage&& other) = delete;
            ThreadLocalStorage& operator=(ThreadLocalStorage&& other) = delete;
};

/*
    Provides :

                static unsigned int get_number_of_logical_cores()
                static unsigned int get_number_of_physical_cores()
                static bool is_hyper_threading()
                static inline void yield()

*/

/*
    Currently this module is not hybrid-architecture-aware
    Ex: P-cores and E-cores starting from Alder Lake
    That means all methods assume that all CPU cores are identical
*/

class ThreadUtilities
{
    public:

        static unsigned int get_number_of_logical_cores()
        {
            unsigned int num_cores{0};
            #ifdef __linux__
            num_cores = sysconf(_SC_NPROCESSORS_ONLN);
            #elif _WIN32
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            num_cores = sysinfo.dwNumberOfProcessors;
            #endif
            return num_cores;
        }

        static unsigned int get_number_of_physical_cores()
        {
            auto num_logical_cores = get_number_of_logical_cores();
            bool cpu_hyperthreading = is_hyper_threading();
            return cpu_hyperthreading ? num_logical_cores / 2 : num_logical_cores;
        }

        static bool is_hyper_threading()
        {
            bool ret = false;

            #ifdef __linux__
            // Using syscalls to avoid dynamic memory allocation
            int file_descriptor = open("/sys/devices/system/cpu/smt/active", O_RDONLY);

            if (file_descriptor != -1)
            {
                char value;
                if (read(file_descriptor, &value, sizeof(value)) > 0)
                {
                    int smt_active = value - '0';
                    ret = (smt_active > 0);
                }

                close(file_descriptor);
            }
            #elif _WIN32
            SYSTEM_INFO sys_info;
            GetSystemInfo(&sys_info);
            char buffer[2048]; // It may be insufficient however even if one logical processor has SMT flag , it means we are hyperthreading
            DWORD buffer_size = sizeof(buffer);

            GetLogicalProcessorInformation(reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(&buffer), &buffer_size);

            DWORD num_system_logical_processors = buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
            for (DWORD i = 0; i < num_system_logical_processors; ++i)
            {
                if (reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(&buffer[i])->Relationship == RelationProcessorCore)
                {
                    if (reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(&buffer[i])->ProcessorCore.Flags == LTP_PC_SMT)
                    {
                        ret = true;
                        break;
                    }
                }
            }
            #endif
            return ret;
        }

        static inline void yield()
        {
            #ifdef __linux__
            sched_yield();
            #elif _WIN32
            SwitchToThread();
            #endif
        }

    private:
};

#ifdef DISPLAY_ENV_VARS
#define MAKE_RED(x)    "\033[0;31m" x "\033[0m"
#define MAKE_BLUE(x)   "\033[0;34m" x "\033[0m"
#define MAKE_YELLOW(x) "\033[0;33m" x "\033[0m"
#endif

class EnvironmentVariable
{
    public:

        // Does not allocate memory
        template <typename T>
        static T get_variable(const char* environment_variable_name, T default_value)
        {
            T value = default_value;
            char* str_value = nullptr;

            #ifdef _WIN32
            // MSVC does not allow std::getenv due to safety
            std::size_t str_value_len;
            errno_t err = _dupenv_s(&str_value, &str_value_len, environment_variable_name);
            if (err)
                return value;
            #elif __linux__
            str_value = std::getenv(environment_variable_name);
            #endif

            if (str_value)
            {
                if constexpr (std::is_arithmetic<T>::value)
                {
                    char* end_ptr = nullptr;
                    auto current_val = std::strtold(str_value, &end_ptr);

                    if (*end_ptr == '\0')
                    {
                        value = static_cast<T>(current_val);
                    }
                }
                else if constexpr (std::is_same<T, char*>::value || std::is_same<T, const char*>::value)
                {
                    value = str_value;
                }
            }

            #ifdef DISPLAY_ENV_VARS
            // Non allocating trace
            fprintf(stderr, MAKE_RED("variable:") " " MAKE_BLUE("%s") ", " MAKE_RED("value:") "  ", environment_variable_name);
            if constexpr (std::is_same<T, double>::value) 
            {
                fprintf(stderr, MAKE_YELLOW("%f") "\n", value);
            }
            else if constexpr (std::is_same<T, std::size_t>::value)
            {
                fprintf(stderr, MAKE_YELLOW("%zu") "\n", value);
            }
            else if constexpr (std::is_arithmetic<T>::value)
            {
                fprintf(stderr, MAKE_YELLOW("%d") "\n", value);
            }
            else
            {
                fprintf(stderr, MAKE_YELLOW("%s") "\n", value);
            }
            #endif

            return value;
        }

        // Utility function when handling csv numeric parameters from environment variables, does not allocate memory
        static void set_numeric_array_from_comma_separated_value_string(std::size_t* target_array, std::size_t array_size, const char* str)
        {
            auto len = strlen(str);

            constexpr std::size_t MAX_STRING_LEN = 64;
            constexpr std::size_t MAX_TOKEN_LEN = 8;
            std::size_t start = 0;
            std::size_t end = 0;
            std::size_t counter = 0;
            
            auto is_string_numeric = [](const char*str)
            {
                auto len = std::strlen(str);
                for(std::size_t i = 0 ; i<len; i++)
                {
                    if (!std::isdigit(static_cast<unsigned char>(str[i]))) 
                    {
                        return false;
                    }
                }
                
                return true;
            };

            while (end <= len && end < MAX_STRING_LEN - 1 && counter <array_size)
            {
                if (str[end] == ',' || (end > start && end == len))
                {
                    char token[MAX_TOKEN_LEN];
                    std::size_t token_len = end - start;

                    #ifdef __linux__
                    strncpy(token, str + start, token_len);
                    #elif _WIN32
                    strncpy_s(token, str + start, token_len);
                    #endif
                    token[token_len] = '\0';
                    
                    if(is_string_numeric(token) == false)
                    {
                        return;
                    }

                    target_array[counter] = atoi(token);

                    start = end + 1;
                    counter++;
                }

                ++end;
            }
        }
};

class AlignmentAndSizeUtils
{
    public:
    
        static constexpr inline std::size_t CPP_DEFAULT_ALLOCATION_ALIGNMENT = 16;

        // Generic check including non pow2
        static bool is_address_aligned(void* address, std::size_t alignment)
        {
            auto address_in_question = reinterpret_cast<uint64_t>( address );
            auto remainder = address_in_question - (address_in_question / alignment) * alignment;
            return remainder == 0;
        }

        static bool is_pow2(std::size_t size)
        {
            return size > 0 && (size & (size - 1)) == 0;
        }

        // Page allocation granularity is always pow2
        static bool is_address_page_allocation_granularity_aligned(void* address)
        {
            assert(is_pow2(VirtualMemory::PAGE_ALLOCATION_GRANULARITY));
            return (reinterpret_cast<uint64_t>(address) & (VirtualMemory::PAGE_ALLOCATION_GRANULARITY - 1)) == 0;
        }

        // Generic check including non pow2
        static bool is_size_a_multiple_of_page_allocation_granularity(std::size_t input)
        {
            auto remainder = input - (input / VirtualMemory::PAGE_ALLOCATION_GRANULARITY) * VirtualMemory::PAGE_ALLOCATION_GRANULARITY;
            return remainder == 0;
        }
        
        static std::size_t get_next_pow2_multiple_of(std::size_t input, std::size_t multiple)
        {
            // Not checking if the given input is already a multiple
            return ((input + multiple - 1) & ~(multiple - 1));
        }
};

/*
    A CAS ( compare-and-swap ) based POD ( https://en.cppreference.com/w/cpp/language/classes#POD_class ) spinlock
    As it is POD , it can be used inside packed declarations.

    To keep it as POD :
                1. Not using standard C++ std::atomic
                2. Member variables should be public

    Otherwise GCC will generate : warning: ignoring packed attribute because of unpacked non-POD field

    PAHOLE OUTPUT :

                size: 4, cachelines: 1, members: 1
                last cacheline: 4 bytes

    Can be faster than os/lock or std::mutex
    However should be picked carefully as it will affect all processes on a CPU core
    even though they are not doing the same computation so misuse may lead to starvation for others

    Doesn`t check against uniprocessors.
*/

// Pass alignment = AlignmentConstants::CPU_CACHE_LINE_SIZE to make the lock cacheline aligned

template<std::size_t alignment=sizeof(uint32_t), std::size_t spin_count = 1024, std::size_t pause_count = 64, bool extra_system_friendly = false>
struct UserspaceSpinlock
{
    // No privates, ctors or dtors to stay as PACKED+POD
    ALIGN_DATA(alignment) uint32_t m_flag=0;

    void initialise()
    {
        m_flag = 0;
    }

    void lock()
    {
        while (true)
        {
            for (std::size_t i(0); i < spin_count; i++)
            {
                if (try_lock() == true)
                {
                    return;
                }

                pause(pause_count);
            }

            if constexpr (extra_system_friendly)
            {
                ThreadUtilities::yield();
            }
        }
    }

    FORCE_INLINE bool try_lock()
    {
        if (builtin_cas(&m_flag, 0, 1) == 1)
        {
            return false;
        }

        return true;
    }

    FORCE_INLINE void unlock()
    {
        m_flag = 0;
    }
};

enum class LockPolicy
{
    NO_LOCK,
    USERSPACE_LOCK,
    USERSPACE_LOCK_CACHELINE_ALIGNED
};

// Since it is a template base class, deriving classes need "this" or full-qualification in order to call its methods
template <LockPolicy lock_policy>
class Lockable
{
public:

    using LockType = std::conditional_t<
        lock_policy == LockPolicy::USERSPACE_LOCK,
        UserspaceSpinlock<>, 
        UserspaceSpinlock<AlignmentConstants::CPU_CACHE_LINE_SIZE>
        >;

    Lockable()
    {
        m_lock.initialise();
    }

    void enter_concurrent_context()
    {
        if constexpr (lock_policy != LockPolicy::NO_LOCK)
        {
            m_lock.lock();
        }
    }

    void leave_concurrent_context()
    {
        if constexpr (lock_policy != LockPolicy::NO_LOCK)
        {
            m_lock.unlock();
        }
    }
private:
    LockType m_lock;
};

// NON THREAD SAFE ITEM QUEUE

template <typename T>
class SinglyLinkedList
{
    public:

        struct SinglyLinkedListNode
        {
            SinglyLinkedListNode* next = nullptr;
            T data;
        };

        void add_free_nodes(char* buffer, std::size_t capacity)
        {
            assert( buffer != nullptr);
            assert( capacity > 0 );

            m_capacity += capacity;

            for (std::size_t i{ 0 }; i < capacity; i++)
            {
                uint64_t address = reinterpret_cast<uint64_t>(buffer + (i * sizeof(SinglyLinkedListNode)) );
                push(reinterpret_cast<SinglyLinkedListNode*>(address));
            }
        }

        bool push(SinglyLinkedListNode* new_node)
        {
            if(m_size<m_capacity)
            {
                new_node->next = m_head;
                m_head = new_node;
                m_size++;
                
                return true;
            }
            
            return false;
        }

        SinglyLinkedListNode* pop()
        {
            if (m_head == nullptr)
            {
                return nullptr;
            }

            SinglyLinkedListNode* top = m_head;
            m_head = m_head->next;
            m_size--;
            return top;
        }

    private:
        ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) SinglyLinkedListNode* m_head = nullptr;
        std::size_t m_capacity = 0;
        std::size_t m_size = 0;
};

template <typename T, typename AllocatorType>
class BoundedQueue
{
    public:

        bool create(std::size_t capacity)
        {
            assert(capacity > 0);

            m_buffer_length = capacity * sizeof(typename SinglyLinkedList<T>::SinglyLinkedListNode) ;
            m_buffer = reinterpret_cast<char*>(AllocatorType::allocate(m_buffer_length));

            if(m_buffer == nullptr)
            {
                return false;
            }

            m_freelist.add_free_nodes(m_buffer, capacity);

            return true;
        }

        ~BoundedQueue()
        {
            if(m_buffer)
            {
                AllocatorType::deallocate(m_buffer, m_buffer_length);
            }
        }

        bool try_push(T t)
        {
            auto free_node = m_freelist.pop();

            if(free_node)
            {
                free_node->data = t;
                free_node->next = m_head;
                m_head = free_node;
                return true;
            }

            return false;
        }

        bool try_pop(T& t)
        {
            if (m_head == nullptr)
            {
                return false;
            }
            
            t = m_head->data;
            
            auto old_head = m_head;
            m_head = m_head->next;
            old_head->next = nullptr;
            m_freelist.push(old_head);
            
            return true;
        }
    
    private:
        ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) typename SinglyLinkedList<T>::SinglyLinkedListNode* m_head = nullptr;
        char* m_buffer = nullptr;
        std::size_t m_buffer_length = 0;
        SinglyLinkedList<T> m_freelist; // Underlying memory
};

/*
    REFERENCE : THIS CODE IS A COSMETICALLY MODIFIED VERSION OF ERIK RIGTORP'S IMPLEMENTATION : https://github.com/rigtorp/MPMCQueue/ ( MIT Licence )
*/

template <typename T> struct Slot 
{
    ~Slot() 
    {
        if (turn & 1) 
        {
            destroy();
        }
    }

    template <typename... Args> void construct(Args &&...args) 
    {
        new (&storage) T(std::forward<Args>(args)...); // Placement new
    }

    void destroy() 
    {
        reinterpret_cast<T*>(&storage)->~T();
    }

    T&& move() { return reinterpret_cast<T&&>(storage); }

    ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) std::atomic<std::size_t> turn = { 0 };
    typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;
};

template <typename T, typename AllocatorType>
class MPMCBoundedQueue
{
public:

    MPMCBoundedQueue() 
    {
        static_assert( alignof(Slot<T>) == AlignmentConstants::CPU_CACHE_LINE_SIZE, "Slot must be aligned to cache line boundary to prevent false sharing");
        static_assert(sizeof(Slot<T>) % AlignmentConstants::CPU_CACHE_LINE_SIZE == 0, "Slot size must be a multiple of cache line size to prevent false sharing between adjacent slots");
        static_assert(sizeof(MPMCBoundedQueue) % AlignmentConstants::CPU_CACHE_LINE_SIZE == 0, "Queue size must be a multiple of cache line size to prevent false sharing between adjacent queues");
        static_assert(offsetof(MPMCBoundedQueue, m_tail) - offsetof(MPMCBoundedQueue, m_head) == static_cast<std::ptrdiff_t>(AlignmentConstants::CPU_CACHE_LINE_SIZE), "head and tail must be a cache line apart to prevent false sharing");
    }

    bool create(const std::size_t capacity)
    {
        m_capacity = capacity;

        if (m_capacity < 1)
        {
            return false;
        }

        m_slots = static_cast<Slot<T>*>(AllocatorType::allocate((m_capacity + 1) * sizeof(Slot<T>)));

        if (reinterpret_cast<size_t>(m_slots) % alignof(Slot<T>) != 0)
        {
            AllocatorType::deallocate(m_slots, (m_capacity + 1) * sizeof(Slot<T>));
            return false;
        }

        for (size_t i = 0; i < m_capacity; ++i)
        {
            new (&m_slots[i]) Slot<T>(); // Placement new
        }

        return true;
    }

    ~MPMCBoundedQueue()
    {
        for (size_t i = 0; i < m_capacity; ++i)
        {
            m_slots[i].~Slot();
        }

        AllocatorType::deallocate(m_slots, (m_capacity + 1) * sizeof(Slot<T>));
    }

    template <typename... Args> void emplace(Args &&...args) 
    {
        auto const head = m_head.fetch_add(1);
        auto& slot = m_slots[modulo_capacity(head)];

        while (turn(head) * 2 != slot.turn.load(std::memory_order_acquire))
            ;

        slot.construct(std::forward<Args>(args)...);
        slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
    }

    template <typename... Args> bool try_emplace(Args &&...args)
    {
        auto head = m_head.load(std::memory_order_acquire);

        for (;;) 
        {
            auto& slot = m_slots[modulo_capacity(head)];
            if (turn(head) * 2 == slot.turn.load(std::memory_order_acquire)) 
            {
                if (m_head.compare_exchange_strong(head, head + 1))
                {
                    slot.construct(std::forward<Args>(args)...);
                    slot.turn.store(turn(head) * 2 + 1, std::memory_order_release);
                    return true;
                }
            }
            else 
            {
                auto const prev_head = head;
                head = m_head.load(std::memory_order_acquire);

                if (head == prev_head)
                {
                    return false;
                }
            }
        }
    }

    void push(const T& v) 
    {
        emplace(v);
    }

    bool try_push(const T& v) 
    {
        return try_emplace(v);
    }

    bool try_pop(T& v)  
    {
        auto tail = m_tail.load(std::memory_order_acquire);

        for (;;) 
        {
            auto& slot = m_slots[modulo_capacity(tail)];

            if (turn(tail) * 2 + 1 == slot.turn.load(std::memory_order_acquire))
            {
                if (m_tail.compare_exchange_strong(tail, tail + 1)) 
                {
                    v = slot.move();
                    slot.destroy();
                    slot.turn.store(turn(tail) * 2 + 2, std::memory_order_release);
                    return true;
                }
            }
            else 
            {
                auto const prev_tail = tail;

                tail = m_tail.load(std::memory_order_acquire);

                if (tail == prev_tail)
                {
                    return false;
                }
            }
        }
    }

    std::size_t size() const
    {
        return static_cast<std::size_t>(m_head.load(std::memory_order_relaxed) - m_tail.load(std::memory_order_relaxed));
    }

private:
    FORCE_INLINE std::size_t modulo_capacity(std::size_t input) const
    {
        assert(m_capacity > 0);
        return input - (input / m_capacity) * m_capacity;
    }

    FORCE_INLINE std::size_t turn(std::size_t i) const { return i / m_capacity; }

    std::size_t m_capacity = 0;
    Slot<T>* m_slots = nullptr;

    ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) std::atomic<std::size_t> m_head = 0;
    ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) std::atomic<std::size_t> m_tail = 0;
};

template <typename T, typename Enable = void>
struct MurmurHash3 
{
    std::size_t operator()(T h) const noexcept 
    {
        h ^= h >> 16;
        h *= 0x85ebca6b;
        h ^= h >> 13;
        h *= 0xc2b2ae35;
        h ^= h >> 16;
        return static_cast<std::size_t>(h);
    }
};

// 64 bit specialisation
template <typename T>
struct MurmurHash3<T, typename std::enable_if<std::is_same<T, uint64_t>::value>::type>
{
    std::size_t operator()(uint64_t h) const noexcept 
    {
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccd;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53;
        h ^= h >> 33;
        return static_cast<std::size_t>(h);
    }
};

/*
    - MPMC THREAD SAFE HOWEVER DESIGNED FOR A CERTAIN SCENARIO THEREFORE DON'T USE IT SOMEWHERE ELSE !
      
      USE CASE : WHEN INSERTS ARE VERY RARE AND SEARCHS ARE VERY FREQUENT, AND WHEN IT IS GUARANTEED THAT 
      SEARCH FOR A SPECIFIC ITEM WILL ALWAYS GUARANTEEDLY BE CALLED AFTER ITS INSERTION :

            - INSERTS ARE PROTECTED BY A SPINLOCK SO NO ABA RISK

            - USES SEPARATE CHAINING WITH ATOMIC LINKED LIST NODES AND ATOMIC HEAD AND CAS TO MAKE SEARCHS LOCKFREE WHILE THERE ARE ONGOING INSERTIONS

            - FIXED SIZE BUCKETS/TABLE WITH NO GROWS SO THERE IS NO RISK OF A GROW AND REHASHING FROM AN INSERTION CALLSTACK DURING A SEARCH,
              HOWEVER TABLE SIZE SHOULD BE CHOSEN CAREFULLY OTHERWISE COLLISIONS CAN DEGRADE THE PERFORMANCE

            - DTOR IS NOT THREAD SAFE HOWEVER IT IS TIED TO THE END OF HOST PROGRAM

    - ITEMS REMOVALS & OBJECTS CTORS WITH ARGUMENTS NOT SUPPORTED

    - DEFAULT HASH FUNCTION : MurmurHash3 https://en.wikipedia.org/wiki/MurmurHash
*/

template <typename Key, typename Value, typename Allocator, typename HashFunction = MurmurHash3<Key>>
class MPMCDictionary 
{
    public:

        struct DictionaryNode
        {
            Key key;
            Value value;
            std::atomic<DictionaryNode*> next = nullptr;

            DictionaryNode() = default;
        };

        MPMCDictionary() = default;

        ~MPMCDictionary()
        {
            if (m_node_cache)
            {
                Allocator::deallocate(m_node_cache, sizeof(DictionaryNode) * m_node_cache_capacity);
            }

            if (m_table)
            {
                Allocator::deallocate(m_node_cache, m_table_size * sizeof(std::atomic<DictionaryNode*>));
            }
        }

        bool initialise(std::size_t capacity)
        {
            assert(capacity > 0);

            m_node_cache_capacity = capacity;
            m_table_size = m_node_cache_capacity;

            m_table = reinterpret_cast<std::atomic<DictionaryNode*>*>(Allocator::allocate(m_table_size * sizeof(std::atomic<DictionaryNode*>)));

            if (m_table == nullptr)
            {
                return false;
            }

            for (std::size_t i = 0; i < m_table_size; i++)
            {
                m_table[i].store(nullptr, std::memory_order_relaxed);
            }

            m_insertion_lock.initialise();

            if (build_node_cache() == false)
            {
                return false;
            }

            return true;
        }

        bool insert(const Key& key, const Value& value) 
        {
            assert(m_table && m_table_size > 0);

            m_insertion_lock.lock();
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            if (m_node_cache_index >= m_node_cache_capacity)
            {
                if (unlikely(build_node_cache() == false))
                {
                    m_insertion_lock.unlock();
                    return false;
                }
            }

            DictionaryNode* new_node = m_node_cache + m_node_cache_index;
            new_node->key = key;
            new_node->value = value;

            std::size_t index = hash(key);
            DictionaryNode* old_head = m_table[index].load(std::memory_order_relaxed);

            do
            {
                new_node->next.store(old_head, std::memory_order_relaxed);
            }
            while (!m_table[index].compare_exchange_weak(old_head, new_node, std::memory_order_release, std::memory_order_relaxed));

            ++m_node_cache_index;
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            m_insertion_lock.unlock();
            return true;
        }

        bool get(const Key& key, Value& value) const 
        {
            assert(m_table && m_table_size > 0);

            std::size_t index = hash(key);

            DictionaryNode* current = m_table[index].load(std::memory_order_acquire);

            while (current) 
            {
                if (current->key == key) 
                {
                    value = current->value;
                    return true;
                }

                current = current->next.load(std::memory_order_acquire);
            }

            return false;
        }

    private:
        ALIGN_DATA(AlignmentConstants::CPU_CACHE_LINE_SIZE) std::atomic<DictionaryNode*>* m_table = nullptr;
        std::size_t m_table_size = 0;

        HashFunction m_hash;
        UserspaceSpinlock<> m_insertion_lock;

        DictionaryNode* m_node_cache = nullptr;
        std::size_t m_node_cache_index = 0;
        std::size_t m_node_cache_capacity = 0;

        bool build_node_cache()
        {
            auto new_node_cache = reinterpret_cast<DictionaryNode*>(Allocator::allocate(sizeof(DictionaryNode) * m_node_cache_capacity));

            if (new_node_cache == nullptr)
            {
                return false;
            }
            
            // Construct DictionaryNode objects
            for (std::size_t i = 0; i < m_node_cache_capacity; i++)
            {
                DictionaryNode* new_node = new (new_node_cache + i) DictionaryNode(); // Placement new
                UNUSED(new_node);
            }
            
            m_node_cache = new_node_cache;
            m_node_cache_index = 0;
            return true;
        }

        FORCE_INLINE std::size_t hash(const Key& key) const
        {
            auto hash_value = m_hash(key);
            auto result = hash_value - (hash_value / m_table_size) * m_table_size;
            return result;
        }
};

/*
    - Not thread safe
    
    - Memory layout : key value key value key value ...
    
    - Uses separate chaining ( linear chaining ) for collisions
   
    - Reallocates memory when the load factor reaches 1

    - Does not support item removal & types with constructors with arguments
*/

template <typename Key, typename Value, typename Allocator, typename HashFunction = MurmurHash3<Key>>
class Dictionary
{
    public:

        struct DictionaryNode
        {
            Key key;                    // We need to store key hash values of different keys can be same
            Value value;
            DictionaryNode* next = nullptr;

            DictionaryNode() = default;
            DictionaryNode(const Key& k, const Value& v) : key(k), value(v), next(nullptr) {}
        };

        Dictionary() = default;

        ~Dictionary()
        {
            destroy();
        }

        bool initialise(std::size_t size)
        {
            return grow(size);
        }

        bool insert(const Key& key, const Value& value)
        {
            assert(m_table_size > 0 && m_node_cache != nullptr);

            if (unlikely(m_item_count == m_table_size)) // Load factor 1 , we need to resize
            {
                if (grow(m_table_size * 2) == false)
                {
                    return false;
                }
            }

            std::size_t index = modulo_table_size(m_hash(key));

            DictionaryNode* new_node = m_node_cache + m_item_count;
            new_node->key = key;
            new_node->value = value;
            
            new_node->next = m_table[index];
            m_table[index] = new_node;

            ++m_item_count;
            return true;
        }

        bool get(const Key& key, Value& value) const
        {
            assert(m_table_size > 0 && m_node_cache != nullptr);

            std::size_t index = modulo_table_size(m_hash(key));
            DictionaryNode* current = m_table[index];

            while (current != nullptr)
            {
                if (current->key == key)
                {
                    value = current->value;
                    return true;
                }
                current = current->next;
            }
            return false;
        }

    private:
        // Members will always be accessed by a single thread , hence no cpu cache line size alignment
        DictionaryNode** m_table = nullptr;
        DictionaryNode* m_node_cache = nullptr;

        std::size_t m_table_size = 0;
        std::size_t m_item_count = 0;

        HashFunction m_hash;

        bool grow(std::size_t size)
        {
            assert(size > 0);

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // CREATE

            auto new_node_cache = reinterpret_cast<DictionaryNode*>(Allocator::allocate(size * sizeof(DictionaryNode)));

            if (new_node_cache == nullptr)
            {
                return false;
            }

            auto new_table = reinterpret_cast<DictionaryNode**>(Allocator::allocate(size * sizeof(DictionaryNode*)));

            if (new_table == nullptr)
            {
                Allocator::deallocate(new_node_cache, size * sizeof(DictionaryNode*));
                return false;
            }

            for (std::size_t i = 0; i < size; ++i)
            {
                new_table[i] = nullptr;
            }

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // COPY EXISTING
            std::size_t copy_count = 0;

            if (m_table != nullptr)
            {
                for (std::size_t i = 0; i < m_table_size; ++i)
                {
                    DictionaryNode* current = m_table[i];

                    while (current != nullptr)
                    {
                        auto current_key_hash = m_hash(current->key);
                        std::size_t new_index = current_key_hash - (current_key_hash / size) * size; // Equivalent of current_key_hash% size;
                       
                        DictionaryNode* new_node = new (new_node_cache + copy_count) DictionaryNode(current->key, current->value); // Placement new
                        copy_count++;

                        new_node->next = new_table[new_index];
                        new_table[new_index] = new_node;

                        current = current->next;
                    }
                }
            }

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // CONSTRUCT THE REST
            for (std::size_t i = copy_count; i < size; i++)
            {
                DictionaryNode* new_node = new (new_node_cache + i) DictionaryNode(); // Placement new
                UNUSED(new_node);
            }

            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // DESTROY EXISTING
            destroy();
            
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // SWAP POINTERS
            m_table = new_table;
            m_node_cache = new_node_cache;
            m_table_size = size;

            return true;
        }

        FORCE_INLINE std::size_t modulo_table_size(std::size_t input) const
        {
            assert(m_table_size > 0);
            return input - (input / m_table_size) * m_table_size;
        }

        void destroy()
        {
            if (m_table)
            {
                Allocator::deallocate(m_table , m_table_size * sizeof(DictionaryNode*) );
            }

            if (m_node_cache)
            {
                if constexpr (std::is_destructible<DictionaryNode>::value)
                {
                    for (std::size_t i = 0; i < m_table_size; ++i)
                    {
                        m_node_cache[i].~DictionaryNode();
                    }
                }

                Allocator::deallocate(m_node_cache, m_table_size * sizeof(DictionaryNode));
            }
        }
};

/*
    - IT RELEASES ONLY UNUSED PAGES. RELEASING USED PAGES IS UP TO THE CALLERS.

    - IF HUGE PAGE IS SPECIFIED AND IF THAT HUGE PAGE ALLOCATION FAILS, WE WILL FAILOVER TO A REGULAR PAGE ALLOCATION
    
    - CAN BE NUMA AWARE IF SPEFICIED

    - LINUX ALLOCATION GRANULARITY IS 4KB (4096) , OTH IT IS 64KB ( 16 * 4096 ) ON WINDOWS .
      REGARDING WINDOWS PAGE ALLOCATION GRANULARITY : https://devblogs.microsoft.com/oldnewthing/20031008-00/?p=42223
*/

struct ArenaOptions
{
    std::size_t cache_capacity = 1024*1024*1024;
    std::size_t page_alignment = 65536;
    bool use_huge_pages = false;
    int numa_node = -1; // -1 means no NUMA
};

class Arena : public Lockable<LockPolicy::USERSPACE_LOCK> // MAINTAINS A SHARED CACHE THEREFORE WE NEED LOCKING
{
    public:

        Arena()
        {
            m_vm_page_size = VirtualMemory::get_page_size(); // DEFAULT VALUE
            m_page_alignment = VirtualMemory::PAGE_ALLOCATION_GRANULARITY;
        }

        ~Arena()
        {
            destroy();
        }

        Arena(const Arena& other) = delete;
        Arena& operator= (const Arena& other) = delete;
        Arena(Arena&& other) = delete;
        Arena& operator=(Arena&& other) = delete;

        [[nodiscard]] bool create(const ArenaOptions& arena_options)
        {
            if (AlignmentAndSizeUtils::is_size_a_multiple_of_page_allocation_granularity(arena_options.page_alignment) == false)
            {
                return false;
            }

            m_page_alignment = arena_options.page_alignment;
            m_use_huge_pages = arena_options.use_huge_pages;
            m_numa_node = arena_options.numa_node;

            this->enter_concurrent_context();
            //////////////////////////////////////////////////
            auto ret =  build_cache(arena_options.cache_capacity);
            //////////////////////////////////////////////////
            this->leave_concurrent_context();

            return ret;
        }

        [[nodiscard]] char* allocate(std::size_t size)
        {
            this->enter_concurrent_context();
            //////////////////////////////////////////////////
            if (size + m_page_alignment > (m_cache_size - m_cache_used_size))
            {
                destroy();

                if (!build_cache(size))
                {
                    this->leave_concurrent_context();
                    return nullptr;
                }
            }

            auto ret = m_cache_buffer + m_cache_used_size;
            m_cache_used_size += size;
            //////////////////////////////////////////////////
            this->leave_concurrent_context();

            assert_msg(AlignmentAndSizeUtils::is_address_aligned(ret, m_page_alignment), "Arena should not return an address which is not aligned to its page alignment setting.");

            return ret;
        }

        [[nodiscard]] char* allocate_aligned(std::size_t size, std::size_t alignment)
        {
            assert_msg(AlignmentAndSizeUtils::is_size_a_multiple_of_page_allocation_granularity(alignment), "Special alignment value requested from Arena should be a multiple of OS page allocation granularity.");

            if(alignment == m_page_alignment)
            {
                return allocate(size);
            }
            else
            {
                assert_msg(AlignmentAndSizeUtils::is_size_a_multiple_of_page_allocation_granularity(m_page_alignment), "Special alignment value requested from Arena should be a multiple of Arena's page alignment value.");

                auto ptr = reinterpret_cast<uint64_t>(allocate(size + alignment));

                if(ptr == 0)
                {
                    return nullptr;
                }

                std::size_t remainder = ptr - ((ptr / alignment) * alignment);
                std::size_t offset = alignment - remainder;
                return reinterpret_cast<char*>(ptr + offset);
            }
        }

        std::size_t page_size()const { return m_vm_page_size; }
        std::size_t page_alignment() const { return m_page_alignment; }

        void release_to_system(void* address, std::size_t size)
        {
            #ifdef NDEBUG
            VirtualMemory::deallocate(address, size);
            #else
            auto release_success = VirtualMemory::deallocate(address, size);
            assert_msg(release_success, "Failure to release pages can lead to system wide issues\n");
            #endif
        }

        class MetadataAllocator
        {
            public:
                static void* allocate(std::size_t size, void* hint_address = nullptr)
                {
                    return VirtualMemory::allocate(size, false, -1, hint_address); // No hugepage, no NUMA
                }

                static void deallocate(void* address, std::size_t size)
                {
                    UNUSED(address);
                    UNUSED(size);
                }
        };

    private:
        std::size_t m_vm_page_size = 0;
        std::size_t m_page_alignment = 0;
        char* m_cache_buffer = nullptr;
        std::size_t m_cache_size = 0;
        std::size_t m_cache_used_size = 0;
        bool m_use_huge_pages = false;
        int m_numa_node = -1;

        void* allocate_from_system(std::size_t size)
        {
            void* ret = nullptr;
            
            if(m_use_huge_pages)
            {
                ret = static_cast<char*>(VirtualMemory::allocate(size, true, m_numa_node, nullptr));

                // If huge page fails, try regular ones
                if (ret == nullptr)
                {
                    ret = static_cast<char*>(VirtualMemory::allocate(size, false, m_numa_node, nullptr));
                }
            }
            else
            {
                ret = static_cast<char*>(VirtualMemory::allocate(size, false, m_numa_node, nullptr));
            }

            return ret;
        }

        [[nodiscard]] bool build_cache(std::size_t size)
        {
            char* buffer = allocate_aligned_from_system(size, m_page_alignment);

            if (buffer == nullptr)
            {
                return false;
            }

            #ifdef ENABLE_PERF_TRACES
            static bool arena_initialised = false;
            if(arena_initialised == true) // We don't want to report the very first arena initialisation
            {
                fprintf(stderr, "\033[0;31m" "arena build cache virtual memory allocation , size=%zu\n" "\033[0m", size);
            }
            arena_initialised = true;
            #endif

            m_cache_buffer = buffer;
            m_cache_used_size = 0;
            m_cache_size = size;

            return true;
        }

        char* allocate_aligned_from_system(std::size_t size, std::size_t alignment)
        {
            std::size_t actual_size = size + alignment;
            char* buffer{ nullptr };

            buffer = static_cast<char*>(allocate_from_system(actual_size));

            if (buffer == nullptr)
            {
                return nullptr;
            }

            std::size_t remainder = reinterpret_cast<std::size_t>(buffer) % alignment;
            std::size_t delta = 0;

            if (remainder > 0)
            {
                // WE NEED PADDING FOR SPECIFIED PAGE ALIGNMENT
                delta = alignment - remainder;
                // RELEASING PADDING PAGE
                release_to_system(buffer, delta);
            }
            else
            {
                // PADDING IS NOT NEEDED, HENCE THE EXTRA ALLOCATED PAGE IS EXCESS
                release_to_system(buffer + actual_size - alignment, alignment);
            }
            auto ret = buffer + delta;

            assert_msg(AlignmentAndSizeUtils::is_address_aligned(ret, alignment), "Arena's overallocation to get an aligned virtual memory address failed.");

            return ret;
        }
        
        void destroy()
        {
            if (m_cache_size > m_cache_used_size)
            {
                // ARENA IS RESPONSIBLE OF CLEARING ONLY NEVER-REQUESTED PAGES.
                std::size_t release_start_address = reinterpret_cast<std::size_t>(m_cache_buffer + m_cache_used_size);
                std::size_t release_end_address = reinterpret_cast<std::size_t>(m_cache_buffer + m_cache_size);

                for (; release_start_address < release_end_address; release_start_address += m_vm_page_size)
                {
                    release_to_system(reinterpret_cast<void *>(release_start_address), m_vm_page_size);
                }

            }
            m_cache_size = 0;
            m_cache_used_size = 0;
            m_cache_buffer = nullptr;
        }
};

/*
    POD LOGICAL PAGE HEADER
    LOGICAL PAGE HEADERS WILL BE PLACED TO THE FIRST 64 BYTES OF EVERY LOGICAL PAGE

    PAHOLE OUTPUT :
                            size: 64, cachelines: 1, members: 10
                            last cacheline: 64 bytes
*/

enum class LogicalPageHeaderFlags : uint16_t
{
    IS_USED = 0x0001
};

PACKED
(
    struct LogicalPageHeader // No privates , member initialisers, ctors or dtors to stay as PACKED+POD
    {
        // 8 BYTES
        uint64_t m_head;                   // Freelist to track memory
        // 8 BYTES
        uint64_t m_next_logical_page_ptr;  // To be used by an upper layer abstraction (ex: segment span etc ) to navigate between logical pages
        // 8 BYTES
        uint64_t m_prev_logical_page_ptr;  // Same as above
        // 2 BYTES
        uint16_t m_page_flags;             // See enum class LogicalPageHeaderFlags
        // 4 BYTES
        uint32_t m_size_class;             // Used to distinguish non-big size class pages, since logical pages won't be holding objects > page size, 2 bytes will be sufficient
        // 8 BYTES
        uint64_t m_used_size;
        // 8 BYTES
        uint64_t m_logical_page_start_address;
        // 8 BYTES
        uint64_t m_logical_page_size;
        // 8 BYTES
        uint64_t m_last_used_node;
        // 2 BYTES
        uint16_t m_segment_id;

        // Total = 64

        void initialise()
        {
            static_assert(sizeof(LogicalPageHeader) == 64);
            m_head = 0;
            m_next_logical_page_ptr = 0;
            m_prev_logical_page_ptr = 0;
            m_page_flags = 0;
            m_size_class = 0;
            m_used_size = 0;
            m_logical_page_start_address = 0;
            m_logical_page_size = 0;
            m_last_used_node = 0;
            m_segment_id = 0;
        }

        template<LogicalPageHeaderFlags flag>
        void set_flag()
        {
            m_page_flags |= static_cast<uint16_t>(flag);
        }

        template<LogicalPageHeaderFlags flag>
        void clear_flag()
        {
            m_page_flags &= ~static_cast<uint16_t>(flag);
        }

        template<LogicalPageHeaderFlags flag>
        bool get_flag() const
        {
            return (m_page_flags & static_cast<uint16_t>(flag)) != 0;
        }
    }
);

/*
    - IT IS A FIRST-IN-LAST-OUT FREELIST IMPLEMENTATION. IT CAN HOLD ONLY ONE SIZE CLASS.

    - IF THE PASSED BUFFER IS START OF A VIRTUAL PAGE AND THE PASSED SIZE IS A VM PAGE SIZE , THEN IT WILL BE CORRESPONDING TO AN ACTUAL VM PAGE
      IDEAL USE CASE IS ITS CORRESPONDING TO A VM PAGE / BEING VM PAGE ALIGNED. SO THAT A SINGLE PAYLOAD WILL NOT SPREAD TO DIFFERENT VM PAGES.
*/

class LogicalPage
{
    public:
        
        PACKED
        (
            struct LogicalPageNode      // No private members/method to stay as POD+PACKED
            {
                LogicalPageNode* m_next = nullptr;      // When not allocated , first 8 bytes will hold address of the next node
                                                        // When allocated , 8 bytes + chunksize-8 bytes will be available to hold data
            }
        );

        using NodeType = LogicalPageNode;    

        LogicalPage() 
        {
            static_assert( sizeof(LogicalPageHeader) % 16 == 0 ); // That is for ensuring that the framework guarantees minimum 16 byte alignment
            m_page_header.initialise();
        }
        ~LogicalPage() {}

        LogicalPage(const LogicalPage& other) = delete;
        LogicalPage& operator= (const LogicalPage& other) = delete;
        LogicalPage(LogicalPage&& other) = delete;
        LogicalPage& operator=(LogicalPage&& other) = delete;

        // Gets its memory from an external source such as a heap's arena
        [[nodiscard]] bool create(void* buffer, const std::size_t buffer_size, uint32_t size_class)
        {
            // Chunk size can't be smaller than a 'next' pointer-or-offset which is 64bit
            if (buffer == nullptr || buffer_size < size_class || size_class < sizeof(uint64_t))
            {
                return false;
            }

            #ifndef UNIT_TEST
            // Segment should place us to a start of aligned vm page + size of header
            void* buffer_start_including_header = reinterpret_cast<void*>(reinterpret_cast<std::size_t>(buffer) - sizeof(*this));
            assert_msg(AlignmentAndSizeUtils::is_address_page_allocation_granularity_aligned(buffer_start_including_header) == true, "LogicalPage : Segments or heaps should pass buffers which are aligned to OS page allocation granularity.");
            UNUSED(buffer_start_including_header);
            #endif

            this->m_page_header.initialise();
            this->m_page_header.m_size_class = size_class;
            this->m_page_header.m_logical_page_start_address = reinterpret_cast<uint64_t>(buffer);
            this->m_page_header.m_logical_page_size = buffer_size;

            grow(buffer, buffer_size);

            return true;
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(const std::size_t size)
        {
            UNUSED(size); 

            NodeType* free_node = pop();

            if (unlikely(free_node == nullptr))
            {
                return nullptr;
            }

            this->m_page_header.m_used_size += this->m_page_header.m_size_class;

            return  reinterpret_cast<void*>(free_node);
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void deallocate(void* ptr)
        {
            this->m_page_header.m_used_size -= this->m_page_header.m_size_class;
            push(static_cast<NodeType*>(ptr));
        }

        std::size_t get_usable_size(void* ptr) 
        { 
            UNUSED(ptr);
            return  static_cast<std::size_t>(this->m_page_header.m_size_class); 
        }

        bool can_be_recycled() { return m_page_header.get_flag<LogicalPageHeaderFlags::IS_USED>() == false; }

        void mark_as_used() { m_page_header.set_flag<LogicalPageHeaderFlags::IS_USED>();  }
        void mark_as_non_used() { m_page_header.clear_flag<LogicalPageHeaderFlags::IS_USED>(); }

        uint64_t get_used_size() const { return m_page_header.m_used_size; }
        uint32_t get_size_class() { return m_page_header.m_size_class; }
        
        uint16_t get_segment_id() { return m_page_header.m_segment_id; }
        void set_segment_id(const uint16_t id) { m_page_header.m_segment_id = id; }

        uint64_t get_next_logical_page() const { return m_page_header.m_next_logical_page_ptr; }
        void set_next_logical_page(void* address) { m_page_header.m_next_logical_page_ptr = reinterpret_cast<uint64_t>(address); }

        uint64_t get_previous_logical_page() const { return m_page_header.m_prev_logical_page_ptr; }
        void set_previous_logical_page(void* address) { m_page_header.m_prev_logical_page_ptr = reinterpret_cast<uint64_t>(address); }

        #ifdef UNIT_TEST
        NodeType* get_head_node() { return reinterpret_cast<NodeType*>(m_page_header.m_head); };
        #endif

    private:

        LogicalPageHeader m_page_header;

        void grow(void* buffer, std::size_t buffer_size)
        {
            const std::size_t chunk_count = buffer_size / this->m_page_header.m_size_class;

            for (std::size_t i = 0; i < chunk_count; ++i)
            {
                std::size_t address = reinterpret_cast<std::size_t>(buffer) + i * this->m_page_header.m_size_class;
                push(reinterpret_cast<NodeType*>(address));
            }
        }

        FORCE_INLINE void push(NodeType* new_node)
        {
            new_node->m_next = reinterpret_cast<NodeType*>(this->m_page_header.m_head);
            this->m_page_header.m_head = reinterpret_cast<uint64_t>(new_node);
        }

        FORCE_INLINE NodeType* pop()
        {
            if(unlikely(this->m_page_header.m_head == 0))
            {
                return nullptr;
            }

            NodeType* top = reinterpret_cast<NodeType*>(this->m_page_header.m_head);
            this->m_page_header.m_head = reinterpret_cast<uint64_t>(top->m_next);
            return top;
        }
};

/*
    - A SEGMENT IS A COLLECTION OF LOGICAL PAGES. IT ALLOWS TO GROW IN SIZE AND TO RETURN UNUSED LOGICAL PAGES BACK TO THE SYSTEM

    - IT WILL PLACE A LOGICAL PAGE HEADER TO INITIAL 64 BYTES OF EVERY LOGICAL PAGE.            
     
    ! IMPORTANT : THE EXTERNAL BUFFER SHOULD BE ALIGNED TO LOGICAL PAGE SIZE. THAT IS CRITICAL FOR REACHING LOGICAL PAGE HEADERS
*/

struct SegmentCreationParameters
{
    std::size_t m_logical_page_size = 0;
    std::size_t m_logical_page_count = 0;
    std::size_t m_page_recycling_threshold = 0;
    uint32_t m_size_class = 0;
    double m_grow_coefficient = 2.0; // 0 means that we will be growing by allocating only required amount. Applies to segments that can grow
    bool m_can_grow = true;
};

template <LockPolicy lock_policy>
class Segment : public Lockable<lock_policy>
{
    public:
    
        using LogicalPageType = LogicalPage;
        using ArenaType = Arena;

        Segment()
        {
            m_logical_page_object_size = sizeof(LogicalPageType);
            assert_msg(m_logical_page_object_size == sizeof(LogicalPageHeader), "Segment: Logical page object size should not exceed logical page header size." );

            m_segment_id_counter++;

            // Central heaps and locals heaps use different specialisation for Segments 
            // Therefore different types lead to 2 diff static m_segment_id_counter variables.
            // We need segment ids to be unique across all segments to be able to identify whether a deallocated ptr 
            // belongs to this thread to avoid pushing it into vm pages used on this thread.
            // Otherwise we wouldn't be able to give unused vm pages back to the system.
            if constexpr (lock_policy == LockPolicy::NO_LOCK)
            {
                // Local heap segment
                m_segment_id = m_segment_id_counter;
            }
            else
            {
                // Central heap segment
                m_segment_id = m_segment_id_counter + 32768;
            }
        }

        ~Segment()
        {
            destroy();
        }

        Segment(const Segment& other) = delete;
        Segment& operator= (const Segment& other) = delete;
        Segment(Segment&& other) = delete;
        Segment& operator=(Segment&& other) = delete;

        [[nodiscard]] bool create(char* external_buffer, ArenaType* arena_ptr, const SegmentCreationParameters& params)
        {
            if (params.m_size_class <= 0 || params.m_logical_page_size <= 0 || AlignmentAndSizeUtils::is_size_a_multiple_of_page_allocation_granularity(params.m_logical_page_size) == false
                || params.m_logical_page_count <= 0 || params.m_logical_page_size <= m_logical_page_object_size || !external_buffer || !arena_ptr)
            {
                return false;
            }

            assert_msg(AlignmentAndSizeUtils::is_address_aligned(external_buffer, params.m_logical_page_size) == true, "Segment: Passed buffer is not aligned to specified logical page size. This is a requirement to enable quick access to logical pages from pointers.");

            m_params = params;
            m_arena = arena_ptr;

            if (grow(external_buffer, params.m_logical_page_count) == nullptr)
            {
                return false;
            }

            return true;
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size = 0)
        {
            void* ret = nullptr;

            this->enter_concurrent_context(); // Locking only for central heap
            
            // Next-fit like , we start searching from where we left if possible
            LogicalPageType* iter = m_last_used ? m_last_used : m_head;

            while (iter)
            {
                ret = iter->allocate(size);

                if (ret != nullptr)
                {
                    m_last_used = iter;
                    this->leave_concurrent_context();
                    return ret;
                }

                iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
            }

            // If we started the search from a non-head node,  then we need one more iteration
            ret = allocate_from_start(size);
            this->leave_concurrent_context();

            return ret;
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void deallocate(void* ptr)
        {
            if( m_head == nullptr ) { return;}
            auto affected = get_logical_page_from_address(ptr, m_params.m_logical_page_size);
            assert_msg(affected->get_segment_id() == m_segment_id, "Deleted ptr's segment id should match this segment's id");
            assert_msg(affected->get_usable_size(ptr) == m_params.m_size_class, "Deleted ptr's size class should match this segment's size class");

            this->enter_concurrent_context(); // Locking only for central heap

            affected->deallocate(ptr);

            if (unlikely(affected->get_used_size() == 0))
            {
                affected->mark_as_non_used();

                if (m_logical_page_count > m_params.m_page_recycling_threshold)
                {
                    recycle_logical_page(affected);
                }
            }

            this->leave_concurrent_context();
        }

        bool owns_pointer(void* ptr)
        {
            return get_segment_id_from_address(ptr) == m_segment_id;
        }

        void transfer_logical_pages_from(LogicalPageType* logical_page_head)
        {
            this->enter_concurrent_context();
            ///////////////////////////////////////////////////////////////////
            LogicalPageType* iter = logical_page_head;

            while (iter)
            {
                LogicalPageType* iter_next = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());

                add_logical_page(iter); // Will also update iter's next ptr

                iter = iter_next;
            }
            ///////////////////////////////////////////////////////////////////
            this->leave_concurrent_context();
        }

        // Constant time logical page look up method for finding logical pages if their start addresses are aligned to logical page size
        static LogicalPageType* get_logical_page_from_address(void* ptr, std::size_t logical_page_size)
        {           
            uint64_t orig_ptr = reinterpret_cast<uint64_t>(ptr);
            // Masking below is equivalent of -> orig_ptr - modulo(orig_ptr, logical_page_size);
            uint64_t target_page_address = orig_ptr & ~(logical_page_size - 1);
            LogicalPageType* target_logical_page = reinterpret_cast<LogicalPageType*>(target_page_address);
            return target_logical_page;
        }

        // Constant time size_class look up method for finding logical pages if their start addresses are aligned to logical page size
        static uint32_t get_size_class_from_address(void* ptr, std::size_t logical_page_size)
        {           
            LogicalPageType* target_logical_page = get_logical_page_from_address(ptr, logical_page_size);
            return target_logical_page->get_size_class();
        }

        uint16_t get_segment_id_from_address(void* ptr)
        {
            LogicalPageType* target_logical_page = get_logical_page_from_address(ptr, m_params.m_logical_page_size);
            return target_logical_page->get_segment_id();
        }

        FORCE_INLINE uint16_t get_id() const { return m_segment_id; }
        
        LogicalPageType* get_head_logical_page() { return m_head; }

        #ifdef UNIT_TEST
        std::size_t get_logical_page_count() const { return m_logical_page_count; }
        #endif

    private:
        SegmentCreationParameters m_params;
        uint16_t m_segment_id = 0;
        std::size_t m_logical_page_object_size = 0;
        std::size_t m_logical_page_count = 0;
        LogicalPageType* m_head = nullptr;
        LogicalPageType* m_tail = nullptr;
        LogicalPageType* m_last_used = nullptr;
        static inline uint16_t m_segment_id_counter = 0; // Not thread safe but segments will always be created from a single thread

        ArenaType* m_arena = nullptr;

        // Returns first logical page ptr of the grow
        [[nodiscard]] LogicalPageType* grow(char* buffer, std::size_t logical_page_count)
        {
            assert_msg(AlignmentAndSizeUtils::is_address_aligned(buffer, m_params.m_logical_page_size), "Passed buffer to segment grow should be aligned to the logical page size.");
            LogicalPageType* first_new_logical_page = nullptr;
            LogicalPageType* previous_page = m_tail;
            LogicalPageType* iter_page = nullptr;

            auto create_new_logical_page = [&](char* logical_page_buffer) -> bool
            {
                iter_page = new(logical_page_buffer) LogicalPageType();

                bool success = iter_page->create(logical_page_buffer + m_logical_page_object_size, m_params.m_logical_page_size - m_logical_page_object_size, m_params.m_size_class);

                if (success == false)
                {
                    m_arena->release_to_system(buffer, m_params.m_logical_page_size);
                    return false;
                }

                iter_page->mark_as_used();
                iter_page->set_segment_id(m_segment_id);

                m_logical_page_count++;

                return true;
            };
            /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // FIRST PAGE
            if (unlikely(create_new_logical_page(buffer) == false))
            {
                return nullptr;
            }

            first_new_logical_page = iter_page;

            if (m_head == nullptr)
            {
                // The very first page
                m_head = iter_page;
                m_tail = iter_page;
            }
            else
            {
                previous_page->set_next_logical_page(iter_page);
                iter_page->set_previous_logical_page(previous_page);
            }

            previous_page = iter_page;

            /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            // REST OF THE PAGES
            for (std::size_t i = 1; i < logical_page_count; i++)
            {
                if (create_new_logical_page(buffer + (i * m_params.m_logical_page_size)) == false)
                {
                    return nullptr;
                }

                previous_page->set_next_logical_page(iter_page);
                iter_page->set_previous_logical_page(previous_page);
                previous_page = iter_page;
            }

            m_tail = iter_page;

            return first_new_logical_page;
        }

        void recycle_logical_page(LogicalPageType* affected)
        {
            remove_logical_page(affected);
            affected->~LogicalPageType();
            m_arena->release_to_system(affected, m_params.m_logical_page_size);
            #ifdef ENABLE_PERF_TRACES
            fprintf(stderr, "\033[0;31m" "segment recycling vm page, size=%zu  sizeclass=%u\n" "\033[0m", m_params.m_logical_page_size, m_params.m_size_class);
            #endif
        }

        void remove_logical_page(LogicalPageType* affected)
        {
            auto next = reinterpret_cast<LogicalPageType*>(affected->get_next_logical_page());
            auto previous = reinterpret_cast<LogicalPageType*>(affected->get_previous_logical_page());

            if (affected == m_last_used)
            {
                if (previous)
                {
                    m_last_used = previous;
                }
                else if (next)
                {
                    m_last_used = next;
                }
                else
                {
                    m_last_used = nullptr;
                }
            }

            if (previous == nullptr)
            {
                m_head = next;

                if (m_head == nullptr || m_head->get_next_logical_page() == 0)
                {
                    m_tail = m_head;
                }
            }
            else
            {
                previous->set_next_logical_page(next);

                if (m_tail == affected)
                {
                    m_tail = previous;
                }
            }

            if (next)
                next->set_previous_logical_page(previous);

            m_logical_page_count--;
        }

        void add_logical_page(LogicalPageType* logical_page)
        {
            if (m_tail)
            {
                m_tail->set_next_logical_page(logical_page);
                logical_page->set_previous_logical_page(m_tail);
            }
            else
            {
                m_head = logical_page;
                m_tail = logical_page;
            }

            logical_page->set_next_logical_page(nullptr);
            m_logical_page_count++;
        }

        void destroy()
        {
            if (m_head == nullptr)
            {
                return;
            }

            LogicalPageType* iter = m_head;
            LogicalPageType* next = nullptr;

            while (iter)
            {
                next = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
                
                if (iter->get_used_size() == 0)
                {
                    // Invoking dtor of logical page
                    iter->~LogicalPageType();
                    // Release pages back to system if we are managing the arena
                    m_arena->release_to_system(iter, m_params.m_logical_page_size);
                }

                iter = next;
            }

            m_head = nullptr;
            m_tail = nullptr;
        }

        // Slow path removal function
        void* allocate_from_start(std::size_t size)
        {
            if (m_last_used)
            {
                void* ret = nullptr;
                LogicalPageType* iter = m_head;

                while (iter != m_last_used)
                {
                    ret = iter->allocate(size);

                    if (ret != nullptr)
                    {
                        m_last_used = iter;
                        return ret;
                    }

                    iter = reinterpret_cast<LogicalPageType*>(iter->get_next_logical_page());
                }
            }

            // If we reached here , it means that we need to allocate more memory
            return allocate_by_growing(size);
        }

        // Slow path removal function
        void* allocate_by_growing(std::size_t size)
        {
            void* ret = nullptr;

            if (m_params.m_can_grow == true)
            {
                std::size_t new_logical_page_count = 0;
                std::size_t minimum_new_logical_page_count = 0;
                calculate_quantities(size, new_logical_page_count, minimum_new_logical_page_count);

                char* new_buffer = nullptr;
                new_buffer = static_cast<char*>(m_arena->allocate_aligned(m_params.m_logical_page_size * new_logical_page_count, m_params.m_logical_page_size));

                if (new_buffer == nullptr && new_logical_page_count > minimum_new_logical_page_count)  // Meeting grow_coefficient is not possible so lower the new_logical_page_count
                {
                    new_logical_page_count = minimum_new_logical_page_count;
                    new_buffer = static_cast<char*>(m_arena->allocate_aligned(m_params.m_logical_page_size * new_logical_page_count, m_params.m_logical_page_size));
                }

                if (!new_buffer)
                {
                    return nullptr;
                }

                auto first_new_logical_page = grow(new_buffer, new_logical_page_count);

                #ifdef ENABLE_PERF_TRACES
                fprintf(stderr, "\033[0;31m" "segment grow size=%zu  sizeclass=%u\n" "\033[0m", size, m_params.m_size_class);
                #endif

                if (first_new_logical_page)
                {
                    ret = first_new_logical_page->allocate(size);

                    if (ret != nullptr)
                    {
                        m_last_used = first_new_logical_page;
                        return ret;
                    }
                }
            }

            // OUT OF MEMORY !
            return nullptr;
        }

        void calculate_quantities(const std::size_t size, std::size_t& desired_new_logical_page_count, std::size_t& minimum_new_logical_page_count)
        {
            minimum_new_logical_page_count = get_required_page_count_for_allocation(m_params.m_logical_page_size, m_logical_page_object_size, m_params.m_size_class, size / m_params.m_size_class);

            if ( likely(m_params.m_grow_coefficient > 0))
            {
                desired_new_logical_page_count = static_cast<std::size_t>(m_logical_page_count * m_params.m_grow_coefficient);

                if (desired_new_logical_page_count < minimum_new_logical_page_count)
                {
                    desired_new_logical_page_count = minimum_new_logical_page_count;
                }
            }
            else
            {
                desired_new_logical_page_count = minimum_new_logical_page_count;
            }
        }

        static std::size_t get_required_page_count_for_allocation(std::size_t page_size, std::size_t page_header_size, std::size_t object_size, std::size_t object_count)
        {
            std::size_t object_count_per_page = static_cast<std::size_t>(std::ceil( (page_size - page_header_size) / object_size));
            std::size_t needed_page_count = static_cast<std::size_t>(std::ceil(static_cast<double>(object_count) / static_cast<double>(object_count_per_page)));

            if (needed_page_count == 0)
            {
                needed_page_count = 1;
            }

            return needed_page_count;
        }
};

/*
    - THE PASSED ALLOCATOR WILL HAVE A CENTRAL HEAP AND ALSO THREAD LOCAL HEAPS.

    - ALLOCATIONS INITIALLY WILL BE FROM LOCAL ( EITHER THREAD LOCAL ) HEAPS. IF LOCAL HEAPS ARE EXHAUSTED , THEN CENTRAL HEAP WILL BE USED.

    - USES CONFIGURABLE METADATA ( DEFAULT 256KB ) TO STORE THREAD LOCAL HEAPS. ALSO INITIALLY USES 64KB METADATA TO STORE THE CENTRAL HEAP
*/

template <typename CentralHeapType, typename LocalHeapType>
class ScalableAllocator : public Lockable<LockPolicy::USERSPACE_LOCK>
{
public:

    using ArenaType = Arena;

    // THIS CLASS IS INTENDED TO BE USED DIRECTLY IN MALLOC REPLACEMENTS
    // SINCE THIS ONE IS A TEMPLATE CLASS , WE HAVE TO ENSURE A SINGLE ONLY STATIC VARIABLE INITIALISATION
    FORCE_INLINE  static ScalableAllocator& get_instance()
    {
        static ScalableAllocator instance;
        return instance;
    }

    [[nodiscard]] bool create(const typename CentralHeapType::HeapCreationParams& params_central, const typename LocalHeapType::HeapCreationParams& params_local, const ArenaOptions& arena_options, std::size_t metadata_buffer_size = 262144)
    {
        if (arena_options.cache_capacity <= 0 || arena_options.page_alignment <= 0 || metadata_buffer_size <= 0 || !AlignmentAndSizeUtils::is_size_a_multiple_of_page_allocation_granularity(arena_options.page_alignment) || !AlignmentAndSizeUtils::is_size_a_multiple_of_page_allocation_granularity(metadata_buffer_size))
        {
            return false;
        }

        if (m_objects_arena.create(arena_options) == false)
        {
            return false;
        }

        m_metadata_buffer_size = metadata_buffer_size;
        m_metadata_buffer = reinterpret_cast<char*>(ArenaType::MetadataAllocator::allocate(m_metadata_buffer_size));

        if (m_metadata_buffer == nullptr)
        {
            return false;
        }

        m_central_heap_buffer = reinterpret_cast<char*>(ArenaType::MetadataAllocator::allocate(65536));

        if(m_central_heap_buffer == nullptr)
        {
            return false;
        }

        m_central_heap = new(m_central_heap_buffer) CentralHeapType();    // Placement new

        if (m_central_heap->create(params_central, &m_objects_arena) == false)
        {
            return false;
        }

        if (ThreadLocalStorage::get_instance().create(ScalableAllocator::thread_specific_destructor) == false)
        {
            return false;
        }

        m_local_heap_creation_params = params_local;

        if (!create_heaps())
        {
            return false;
        }

        m_initialised_successfully.store(true);

        return true;
    }

    void set_thread_local_heap_cache_count(std::size_t count)
    {
        m_cached_thread_local_heap_count = count;
    }
    
    void set_enable_fast_shutdown(bool b) { m_fast_shutdown = b; }
    bool get_enable_fast_shutdown() const { return m_fast_shutdown; }
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
    void* allocate(const std::size_t size)
    {
        void* ret{ nullptr };
        auto local_heap = get_thread_local_heap();

        if (local_heap != nullptr)
        {
            ret = local_heap->allocate(size);
        }

        if (ret == nullptr)
        {
            #ifdef ENABLE_PERF_TRACES
            m_central_heap_hit_count++;
            fprintf(stderr, "\033[0;31m" "scalable allocator , central heap hit count=%zu , sizeclass=%zu\n" "\033[0m", m_central_heap_hit_count, size);
            #endif

            //If the local one is exhausted , failover to the central one
            ret = m_central_heap->allocate(size);
        }

        return ret;
    }

    ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
    void deallocate(void* ptr, bool is_small_object = true)
    {
        bool returned_to_local_heap = false;

        auto local_heap = get_thread_local_heap();

        if (local_heap != nullptr)
        {
            returned_to_local_heap = local_heap->deallocate(ptr, is_small_object);
        }

        if(returned_to_local_heap == false)
        {
            m_central_heap->deallocate(ptr, is_small_object);
        }
    }

    CentralHeapType* get_central_heap() { return m_central_heap; }

    #ifdef UNIT_TEST
    std::size_t get_observed_unique_thread_count() const { return m_observed_unique_thread_count; }
    std::size_t get_max_thread_local_heap_count() const { return m_max_thread_local_heap_count; }
    #endif

private:
    char* m_central_heap_buffer = nullptr;
    CentralHeapType* m_central_heap = nullptr;
    ArenaType m_objects_arena;
    char* m_metadata_buffer = nullptr;
    std::size_t m_metadata_buffer_size = 262144;       // Default 256KB
    std::size_t m_active_local_heap_count = 0;
    std::size_t m_max_thread_local_heap_count = 0;    // Used for only thread local heaps
    std::size_t m_cached_thread_local_heap_count = 0; // Used for only thread local heaps , its number of available passive heaps
    bool m_fast_shutdown = true;
    typename LocalHeapType::HeapCreationParams m_local_heap_creation_params;

    static inline std::atomic<bool> m_initialised_successfully = false;
    static inline std::atomic<bool> m_shutdown_started = false;

    #ifdef UNIT_TEST
    std::size_t m_observed_unique_thread_count = 0;
    #endif

    #ifdef ENABLE_PERF_TRACES
    std::size_t m_central_heap_hit_count = 0;
    #endif

    ScalableAllocator()
    {
    }

    ~ScalableAllocator()
    {
        if(m_fast_shutdown)
        {
            return;
        }

        if(m_initialised_successfully.load() == true )
        {
            // We call it here it in case not called earlier and there are still running threads which are not destructed , no need to move logical pages between heaps
            m_shutdown_started.store(true);

            destroy_heaps();

            ThreadLocalStorage::get_instance().destroy();
        }
    }

    ScalableAllocator(const ScalableAllocator& other) = delete;
    ScalableAllocator& operator= (const ScalableAllocator& other) = delete;
    ScalableAllocator(ScalableAllocator&& other) = delete;
    ScalableAllocator& operator=(ScalableAllocator&& other) = delete;
    
    static void thread_specific_destructor(void* arg)
    {
        if(get_instance().get_enable_fast_shutdown() == false)
        {
            if( m_initialised_successfully.load() == true && m_shutdown_started.load() == false )
            {
                auto central_heap = get_instance().get_central_heap();
                auto segment_count = CentralHeapType::get_segment_count();

                auto thread_local_heap = reinterpret_cast<LocalHeapType*>(arg);

                for(std::size_t i =0; i<segment_count; i++)
                {
                    central_heap->get_segment(i)->transfer_logical_pages_from( thread_local_heap->get_segment(i)->get_head_logical_page() );
                }
            }
        }
    }

    std::size_t get_created_heap_count()
    {
        auto heap_count = m_cached_thread_local_heap_count > m_active_local_heap_count ? m_cached_thread_local_heap_count : m_active_local_heap_count;
        return heap_count;
    }

    void destroy_heaps()
    {
        if (m_metadata_buffer)
        {
            auto heap_count = get_created_heap_count();

            for (std::size_t i = 0; i < heap_count; i++)
            {
                LocalHeapType* local_heap = reinterpret_cast<LocalHeapType*>(m_metadata_buffer + (i * sizeof(LocalHeapType)));
                local_heap->~LocalHeapType();
            }

            ArenaType::MetadataAllocator::deallocate(m_metadata_buffer, m_metadata_buffer_size);
        }

        if(m_central_heap_buffer)
        {
            ArenaType::MetadataAllocator::deallocate(m_central_heap_buffer, 65536);
        }
    }

    LocalHeapType* get_thread_local_heap()
    {
        return get_thread_local_heap_internal();
    }

    FORCE_INLINE LocalHeapType* get_thread_local_heap_internal()
    {
        auto thread_local_heap = reinterpret_cast<LocalHeapType*>(ThreadLocalStorage::get_instance().get());

        if (thread_local_heap == nullptr)
        {
            // LOCKING HERE WILL HAPPEN ONLY ONCE FOR EACH THREAD , AT THEIR START
            // AS THERE ARE SHARED VARIABLES FOR THREAD-LOCAL HEAP CREATION
            this->enter_concurrent_context();

            #ifdef UNIT_TEST
            m_observed_unique_thread_count++;
            #endif

            ///////////////////////////////////////////////////////////////////////////////////////////////////////////
            if (m_active_local_heap_count + 1 >= m_max_thread_local_heap_count)
            {
                // If we are here , it means that metadata buffer size is not sufficient to handle all threads of the application
                this->leave_concurrent_context();
                return nullptr;
            }

            if (m_active_local_heap_count >= m_cached_thread_local_heap_count)
            {
                thread_local_heap = create_local_heap(m_active_local_heap_count);
            }
            else
            {
                thread_local_heap = reinterpret_cast<LocalHeapType*>(m_metadata_buffer + (m_active_local_heap_count * sizeof(LocalHeapType)));
            }

            m_active_local_heap_count++;
            ThreadLocalStorage::get_instance().set(thread_local_heap);
            ///////////////////////////////////////////////////////////////////////////////////////////////////////////
            this->leave_concurrent_context();
        }

        return thread_local_heap;
    }

    bool create_heaps()
    {
        m_max_thread_local_heap_count = m_metadata_buffer_size / sizeof(LocalHeapType);

        if (m_max_thread_local_heap_count == 0)
        {
            return false;
        }

        if (m_max_thread_local_heap_count < m_cached_thread_local_heap_count)
        {
            m_cached_thread_local_heap_count = m_max_thread_local_heap_count;
        }

        for (std::size_t i{ 0 }; i < m_cached_thread_local_heap_count; i++)
        {
            auto local_heap = create_local_heap(i);
            if (!local_heap) return false;
        }

        return true;
    }

    LocalHeapType* create_local_heap(std::size_t metadata_buffer_index)
    {
        LocalHeapType* local_heap = new(m_metadata_buffer + (metadata_buffer_index * sizeof(LocalHeapType))) LocalHeapType();    // Placement new

        if (local_heap->create(m_local_heap_creation_params, &m_objects_arena) == false)
        {       
            #ifdef ENABLE_PERF_TRACES
            fprintf(stderr, "\033[0;31m" "scalable allocator , failed to create thread local heap\n" "\033[0m");
            #endif
            
            return nullptr;
        }

        return local_heap;
    }
};

class CompileTimePow2Utils
{
    public :

        template <std::size_t N>
        static constexpr std::size_t compile_time_pow2()
        {
            static_assert(N>=0); // To ensure that it is called in compile time only
            return 1 << N;
        }

        template<unsigned int n>
        static constexpr unsigned int compile_time_log2()
        {
            return (n <= 1) ? 0 : 1 + compile_time_log2<n / 2>();
        }
};

// Template defaults are for thread local or single threaded cases
template<typename DeallocationQueueType = BoundedQueue<uint64_t, typename Arena::MetadataAllocator>, LockPolicy segment_lock_policy = LockPolicy::NO_LOCK> 
class HeapPow2
{
    public:

        HeapPow2() {}
        ~HeapPow2() {}
        HeapPow2(const HeapPow2& other) = delete;
        HeapPow2& operator= (const HeapPow2& other) = delete;
        HeapPow2(HeapPow2&& other) = delete;
        HeapPow2& operator=(HeapPow2&& other) = delete;

        static constexpr std::size_t BIN_COUNT = 15; // Small : 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 , Medium: 65536 131072 262144
        static constexpr std::size_t MAX_BIN_INDEX = BIN_COUNT - 1;
        static constexpr std::size_t MIN_MEDIUM_OBJECT_BIN_INDEX = 12;
        static constexpr inline std::size_t LARGEST_SIZE_CLASS = CompileTimePow2Utils::compile_time_pow2<BIN_COUNT + 3>(); // +3 since we skip bin2 bin4 and bin8 as the size classes start from 16
        static constexpr inline std::size_t LARGEST_SMALL_OBJECT_SIZE_CLASS = CompileTimePow2Utils::compile_time_pow2<MIN_MEDIUM_OBJECT_BIN_INDEX + 3>(); // +3 since we skip bin2 bin4 and bin8 as the size classes start from 16

        static constexpr std::size_t MIN_SIZE_CLASS = 16;
        static constexpr inline std::size_t LOG2_MIN_SIZE_CLASS = CompileTimePow2Utils::compile_time_log2<MIN_SIZE_CLASS>();

        using ArenaType = Arena;
        using SegmentType = Segment<segment_lock_policy>;

        struct HeapCreationParams
        {
            // SIZES AND CAPACITIES
            std::size_t small_object_logical_page_size = 65536; // 64 KB
            std::size_t medium_object_logical_page_size = 524288; // 512 KB
            std::size_t logical_page_counts[BIN_COUNT] = { 1,1,1,1,1, 1,1,2,4,8, 16,32,8,16,32 };
            // RECYCLING AND GROWING
            std::size_t page_recycling_threshold_per_size_class = 1024;
            bool segments_can_grow = true;
            double segment_grow_coefficient = 2.0;
            // DEALLOCATION QUEUES
            std::size_t deallocation_queues_processing_threshold = 1024;
            std::size_t recyclable_deallocation_queue_size = 65536;
            std::size_t non_recyclable_deallocation_queue_size = 65536;
        };

        [[nodiscard]] bool create(const HeapCreationParams& params, ArenaType* arena)
        {
            //////////////////////////////////////////////////////////////////////////////////////////////
            // 1. CHECKS
            assert_msg(arena, "Heap must receive a valid arena instance.");

            // Logical page sizes should be multiples of page allocation granularity ( 4KB on Linux ,64 KB on Windows )
            if (!AlignmentAndSizeUtils::is_size_a_multiple_of_page_allocation_granularity(params.small_object_logical_page_size))
            {
                return false;
            }
            
            if (!AlignmentAndSizeUtils::is_size_a_multiple_of_page_allocation_granularity(params.medium_object_logical_page_size))
            {
                return false;
            }

            m_small_object_logical_page_size = params.small_object_logical_page_size;
            m_medium_object_logical_page_size = params.medium_object_logical_page_size;

            //////////////////////////////////////////////////////////////////////////////////////////////
            // 2. CALCULATE REQUIRED BUFFER SIZE
            std::size_t small_objects_required_buffer_size{ 0 };
            std::size_t medium_objects_required_buffer_size{ 0 };
            std::size_t size_class = MIN_SIZE_CLASS;

            for (std::size_t i = 0; i < BIN_COUNT; i++)
            {
                if(i<MIN_MEDIUM_OBJECT_BIN_INDEX)
                {
                    small_objects_required_buffer_size += (params.logical_page_counts[i] * m_small_object_logical_page_size);
                }
                else
                {
                    medium_objects_required_buffer_size += (params.logical_page_counts[i] * m_medium_object_logical_page_size);
                }

                size_class = size_class << 1;
            }

            //////////////////////////////////////////////////////////////////////////////////////////////
            // 3. ALLOCATE BUFFERS
            auto small_objects_buffer_address = reinterpret_cast<uint64_t>(arena->allocate(small_objects_required_buffer_size));
            assert_msg(AlignmentAndSizeUtils::is_address_page_allocation_granularity_aligned(reinterpret_cast<void*>(small_objects_buffer_address)), "HeapPow2: Arena failed to pass an address which is aligned to OS page allocation granularity.");

            char* medium_objects_buffer_address = reinterpret_cast<char*>(arena->allocate_aligned(medium_objects_required_buffer_size, m_medium_object_logical_page_size));
            assert_msg(AlignmentAndSizeUtils::is_address_page_allocation_granularity_aligned(reinterpret_cast<void*>(medium_objects_buffer_address)), "HeapPow2: Arena failed to pass an address which is aligned to OS page allocation granularity.");
            assert_msg(AlignmentAndSizeUtils::is_address_aligned(reinterpret_cast<void*>(medium_objects_buffer_address), m_medium_object_logical_page_size), "HeapPow2: Failed to get an address which is aligned to medium objects page size.");

            //////////////////////////////////////////////////////////////////////////////////////////////
            // 4. DISTRIBUTE BUFFER TO BINS ,  NEED TO PLACE LOGICAL PAGE HEADERS TO START OF PAGES !

            std::size_t buffer_index{ 0 };
            size_class = MIN_SIZE_CLASS;

            SegmentCreationParameters segment_params;
            segment_params.m_page_recycling_threshold = params.page_recycling_threshold_per_size_class;
            segment_params.m_can_grow = params.segments_can_grow;
            segment_params.m_grow_coefficient = params.segment_grow_coefficient;

            for (std::size_t i = 0; i < MIN_MEDIUM_OBJECT_BIN_INDEX; i++)
            {
                auto required_logical_page_count = params.logical_page_counts[i];
                segment_params.m_size_class = static_cast<uint32_t>(size_class);
                segment_params.m_logical_page_count = required_logical_page_count;

                segment_params.m_logical_page_size = m_small_object_logical_page_size;
                auto bin_buffer_size = required_logical_page_count * m_small_object_logical_page_size;

                bool success = m_segments[i].create(reinterpret_cast<char*>(small_objects_buffer_address) + buffer_index, arena, segment_params);

                if (!success)
                {
                    return false;
                }

                buffer_index += bin_buffer_size;
                size_class = size_class << 1;
            }

            buffer_index = 0;

            for (std::size_t i = MIN_MEDIUM_OBJECT_BIN_INDEX; i < BIN_COUNT; i++)
            {
                auto required_logical_page_count = params.logical_page_counts[i];
                segment_params.m_size_class = static_cast<uint32_t>(size_class);
                segment_params.m_logical_page_count = required_logical_page_count;

                segment_params.m_logical_page_size = m_medium_object_logical_page_size;
                auto bin_buffer_size = required_logical_page_count * m_medium_object_logical_page_size;

                bool success = m_segments[i].create(medium_objects_buffer_address + buffer_index, arena, segment_params);

                if (!success)
                {
                    return false;
                }

                buffer_index += bin_buffer_size;
                size_class = size_class << 1;
            }
            //////////////////////////////////////////////////////////////////////////////////////////////
            // 5. DEALLOCATION QUEUES
            m_deallocation_queue_processing_threshold = params.deallocation_queues_processing_threshold;

            for (std::size_t i = 0; i < BIN_COUNT; i++)
            {
                if(params.non_recyclable_deallocation_queue_size > 0)
                {
                    if (m_non_recyclable_deallocation_queues[i].create(params.non_recyclable_deallocation_queue_size) == false)
                    {
                        return false;
                    }
                }
                
                if (m_recyclable_deallocation_queues[i].create(params.recyclable_deallocation_queue_size) == false)
                {
                    return false;
                }
            }

            return true;
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size = 0)
        {
            size = size < MIN_SIZE_CLASS ? MIN_SIZE_CLASS : size;
            size = get_first_pow2_of(size);
            auto bin_index = get_pow2_bin_index_from_size(size);

            m_potential_pending_max_deallocation_count++;

            if (unlikely(m_potential_pending_max_deallocation_count >= m_deallocation_queue_processing_threshold))
            {
                return allocate_by_processing_deallocation_queues(bin_index, size);
            }

            uint64_t pointer{ 0 };

            if (m_non_recyclable_deallocation_queues[bin_index].try_pop(pointer))
            {
                return reinterpret_cast<void*>(pointer);
            }

            if (m_recyclable_deallocation_queues[bin_index].try_pop(pointer))
            {
                return reinterpret_cast<void*>(pointer);
            }

            return m_segments[bin_index].allocate(size);
        }

        // Slow path removal function
        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void* allocate_by_processing_deallocation_queues(std::size_t bin_index, std::size_t size)
        {
            #ifdef ENABLE_PERF_TRACES
            fprintf(stderr, "\033[0;31m" "Heap processing deallocation queue in allocation callstack\n" "\033[0m");
            #endif

            m_potential_pending_max_deallocation_count = 0;

            auto ret = process_recyclable_deallocation_queue(bin_index);

            if(ret != nullptr)
            {
                return ret;
            }

            uint64_t pointer{ 0 };

            if (m_non_recyclable_deallocation_queues[bin_index].try_pop(pointer))
            {
                return reinterpret_cast<void*>(pointer);
            }

            return m_segments[bin_index].allocate(size);
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        bool deallocate(void* ptr, bool is_small_object)
        {
            auto logical_page_size = is_small_object ? m_small_object_logical_page_size : m_medium_object_logical_page_size;
            auto target_logical_page = SegmentType::get_logical_page_from_address(ptr, logical_page_size);

            auto size_class = target_logical_page->get_size_class();

            assert_msg(size_class >= MIN_SIZE_CLASS, "HeapPow2 deallocate : Found size class is invalid. The pointer may not have been allocated by this allocator.");
            
            auto bin_index = get_pow2_bin_index_from_size(size_class);

            if (m_segments[bin_index].get_id() == target_logical_page->get_segment_id())
            {
                return m_recyclable_deallocation_queues[bin_index].try_push(reinterpret_cast<uint64_t>(ptr));
            }
            else
            {
                return m_non_recyclable_deallocation_queues[bin_index].try_push(reinterpret_cast<uint64_t>(ptr));
            }
        }

        SegmentType* get_segment(std::size_t bin_index)
        {
            return &(m_segments[bin_index]);
        }

        static std::size_t get_segment_count()
        {
            return BIN_COUNT;
        }

        static std::size_t get_max_allocation_size()
        {
            return LARGEST_SIZE_CLASS;
        }

        static std::size_t get_max_small_object_size()
        {
            return LARGEST_SMALL_OBJECT_SIZE_CLASS;
        }

        #ifdef UNIT_TEST
        std::size_t get_bin_logical_page_count(std::size_t bin_index)
        {
            return m_segments[bin_index].get_logical_page_count();
        }
        #endif

    private:
        std::size_t m_small_object_logical_page_size = 0;
        std::size_t m_medium_object_logical_page_size = 0;
        std::array<SegmentType, BIN_COUNT> m_segments;

        std::size_t m_potential_pending_max_deallocation_count = 0; // Not thread safe but doesn't need to be
        std::size_t m_deallocation_queue_processing_threshold = 0;
        std::array<DeallocationQueueType, BIN_COUNT> m_recyclable_deallocation_queues;
        std::array<DeallocationQueueType, BIN_COUNT> m_non_recyclable_deallocation_queues;

        void* process_recyclable_deallocation_queue(std::size_t bin_index)
        {
            void* ret = nullptr;

            while (true)
            {
                uint64_t pointer{ 0 };

                if (m_recyclable_deallocation_queues[bin_index].try_pop(pointer))
                {
                    if (likely(ret != nullptr))
                    {
                        m_segments[bin_index].deallocate(reinterpret_cast<void*>(pointer));
                    }
                    else
                    {
                        ret = reinterpret_cast<void*>(pointer);
                    }
                }
                else
                {
                    break;
                }
            }

            return ret;
        }

        // Reference : https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
        FORCE_INLINE std::size_t get_first_pow2_of(std::size_t input)
        {
            assert(input>= MIN_SIZE_CLASS);

            /* 
            No need for the if check below , as the caller will pass a miniumum of MIN_SIZE_CLASS which is > 1
            if (unlikely(input <= 1))
            {
                return 1;
            }
            */

            input--;
            input |= input >> 1;
            input |= input >> 2;
            input |= input >> 4;
            input |= input >> 8;
            input |= input >> 16;

            return input + 1;
        }

        // IMPLEMENTATION IS FOR 64 BIT ONLY
        FORCE_INLINE std::size_t get_pow2_bin_index_from_size(std::size_t size)
        {
            std::size_t index = static_cast<std::size_t>(63 - builtin_clzl(static_cast<unsigned long>(size))) - LOG2_MIN_SIZE_CLASS;
            index = index > MAX_BIN_INDEX ? MAX_BIN_INDEX : index;
            return index;
        }
};

// Template defaults are for thread local or single threaded cases
template<typename DeallocationQueueType = BoundedQueue<uint64_t, typename Arena::MetadataAllocator>, LockPolicy segment_lock_policy = LockPolicy::NO_LOCK>
class HeapPool
{
    public:
    
        using ArenaType = Arena;

        HeapPool() {}
        ~HeapPool() {}
        HeapPool(const HeapPool& other) = delete;
        HeapPool& operator= (const HeapPool& other) = delete;
        HeapPool(HeapPool&& other) = delete;
        HeapPool& operator=(HeapPool&& other) = delete;

        using SegmentType = Segment<segment_lock_policy>;

        struct HeapCreationParams
        {
            // SIZES AND CAPACITIES
            uint32_t size_class = 0;
            std::size_t initial_size = 0;
            std::size_t logical_page_size = 65536; // 64 KB
            // RECYCLING AND GROWING
            bool segments_can_grow = true;
            std::size_t page_recycling_threshold = 1;
            double grow_coefficient = 2.0;
            // DEALLOCATION QUEUES
            std::size_t recyclable_deallocation_queue_size = 65536;
            std::size_t non_recyclable_deallocation_queue_size = 65536;
            std::size_t deallocation_queues_processing_threshold = 1024;
        };

        [[nodiscard]] bool create(const HeapCreationParams& params, ArenaType* arena_ptr)
        {
            assert_msg(params.size_class > 0, "Pool size class should be greater than zero.");
            assert_msg(params.initial_size > 0, "Pool initial size should be greater than zero.");
            assert_msg(params.initial_size % params.logical_page_size == 0, "Initial pool size should be a multiple of its logical page size.");

            m_arena = arena_ptr;

            auto buffer_length = params.initial_size;
            auto buffer_address = reinterpret_cast<uint64_t>(m_arena->allocate(buffer_length));

            assert_msg(AlignmentAndSizeUtils::is_address_page_allocation_granularity_aligned(reinterpret_cast<void*>(buffer_address)), "Arena failed to return page alloc granularity aligned address for memory pool.");
            
            std::size_t segment_size = static_cast<std::size_t>(buffer_length);
            std::size_t logical_page_count_per_segment = static_cast<std::size_t>(segment_size / params.logical_page_size);

            SegmentCreationParameters segment_params;
            segment_params.m_size_class = params.size_class;
            segment_params.m_logical_page_size = params.logical_page_size;
            segment_params.m_page_recycling_threshold = params.page_recycling_threshold;
            segment_params.m_can_grow = params.segments_can_grow;
            segment_params.m_grow_coefficient = params.grow_coefficient;

            segment_params.m_logical_page_count = logical_page_count_per_segment;

            if (m_segment.create(reinterpret_cast<char*>(buffer_address), m_arena, segment_params) == false)
            {
                return false;
            }
            
            if (m_recyclable_deallocation_queue.create(params.recyclable_deallocation_queue_size/sizeof(uint64_t)) == false)
            {
                return false;
            }
            
            if (m_non_recyclable_deallocation_queue.create(params.non_recyclable_deallocation_queue_size/sizeof(uint64_t)) == false)
            {
                return false;
            }
            
            m_deallocation_queue_processing_threshold = params.deallocation_queues_processing_threshold;

            return true;
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size = 0)
        {
            UNUSED(size);

            m_potential_pending_max_deallocation_count++;

            if(unlikely(m_potential_pending_max_deallocation_count.load() >= m_deallocation_queue_processing_threshold ))
            {
                return allocate_by_processing_deallocation_queue(size);
            }

            uint64_t pointer{ 0 };

            if (m_non_recyclable_deallocation_queue.try_pop(pointer))
            {
                return reinterpret_cast<void*>(pointer);
            }

            if (m_recyclable_deallocation_queue.try_pop(pointer))
            {
                return reinterpret_cast<void*>(pointer);
            }

            return m_segment.allocate(size);
        }

        // Slow path removal function
        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate_by_processing_deallocation_queue(std::size_t size)
        {
            #ifdef ENABLE_PERF_TRACES
            fprintf(stderr, "\033[0;31m" "HeapPoolCentral processing deallocation queue in allocation callstack\n" "\033[0m");
            #endif

            m_potential_pending_max_deallocation_count = 0;
            
            auto ret = process_recyclable_deallocation_queue();
            
            if(ret != nullptr)
            {
                return ret;
            }
            
            uint64_t pointer{ 0 };

            if (m_non_recyclable_deallocation_queue.try_pop(pointer))
            {
                return reinterpret_cast<void*>(pointer);
            }

            return m_segment.allocate(size);
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        bool deallocate(void* ptr, bool is_small_object = false)
        {
            UNUSED(is_small_object);

            if(m_segment.owns_pointer(ptr))
            {
                return m_recyclable_deallocation_queue.try_push(reinterpret_cast<uint64_t>(ptr));
            }
            else
            {
                // We don't want to return this pointer back to segment where virtual memory page recycling happens
                return m_non_recyclable_deallocation_queue.try_push(reinterpret_cast<uint64_t>(ptr));
            }
        }

        static std::size_t get_segment_count()
        {
            return 1;
        }

        SegmentType* get_segment(std::size_t bin_index)
        {
            assert_msg(bin_index>0, "HeapPool holds only a single segment.");
            return &m_segment;
        }

        #ifdef UNIT_TEST
        std::size_t get_logical_page_count() { return m_segment.get_logical_page_count(); }
        #endif

    private:
        SegmentType m_segment;
        ArenaType* m_arena = nullptr;
        std::atomic<std::size_t> m_potential_pending_max_deallocation_count = 0;
        std::size_t m_deallocation_queue_processing_threshold = 65536;
        DeallocationQueueType m_recyclable_deallocation_queue;
        DeallocationQueueType m_non_recyclable_deallocation_queue;

        void* process_recyclable_deallocation_queue()
        {
            void* ret = nullptr;

            while (true)
            {
                uint64_t pointer{ 0 };

                if (m_recyclable_deallocation_queue.try_pop(pointer))
                {
                    if (likely(ret != nullptr))
                    {
                        m_segment.deallocate(reinterpret_cast<void*>(pointer));
                    }
                    else
                    {
                        ret = reinterpret_cast<void*>(pointer);
                    }
                }
                else
                {
                    break;
                }
            }

            return ret;
        }
};

// INTERFACE WRAPPER FOR THREAD CACHING MEMORY POOL

struct ScalablePoolOptions
{
    // SIZE AND CAPACITIES
    std::size_t arena_initial_size = 1024*1024*64;    // 64 MB
    std::size_t central_pool_initial_size = 1024 * 1024 * 16; // 16 MB
    std::size_t local_pool_initial_size = 1024*1024*32; // 32 MB
    // RECYCLING AND GROWING
    bool local_pool_can_grow = true;
    std::size_t page_recycling_threshold = 128;
    double      grow_coefficient = 2.0;
    // DEALLOCATION QUEUES
    std::size_t deallocation_queues_processing_threshold = 409600;
    std::size_t recyclable_deallocation_queue_size = 65536;
    std::size_t non_recyclable_deallocation_queue_size = 65536;
    // OTHERS
    bool use_huge_pages = false;
    int numa_node = -1;
    std::size_t thread_local_cached_heap_count = 0; // If zero, we will use physical core count
};

template <typename T>
class ScalablePool
{
    public:

        using ArenaType = Arena;
        using CentralHeapType = HeapPool<MPMCBoundedQueue<uint64_t, typename ArenaType::MetadataAllocator>, LockPolicy::USERSPACE_LOCK>;
        using LocalHeapType = HeapPool<BoundedQueue<uint64_t, typename ArenaType::MetadataAllocator>, LockPolicy::NO_LOCK>;
        using ScalableMemoryPool = ScalableAllocator<CentralHeapType, LocalHeapType>;

        bool create(ScalablePoolOptions options = ScalablePoolOptions())
        {
            typename LocalHeapType::HeapCreationParams local_heap_params;
            typename CentralHeapType::HeapCreationParams central_heap_params;
            auto logical_page_size = local_heap_params.logical_page_size;

            if(options.use_huge_pages == true)
            {
                auto huge_page_size = VirtualMemory::get_minimum_huge_page_size();
                
                if(options.central_pool_initial_size < huge_page_size  || options.central_pool_initial_size % huge_page_size != 0)
                {
                    return false;
                }
                
                if(options.local_pool_initial_size < huge_page_size  || options.local_pool_initial_size % huge_page_size != 0)
                {
                    return false;
                }

                logical_page_size  = huge_page_size;
            }

            uint32_t size_class = sizeof(T) >= sizeof(uint64_t) ? sizeof(T) : sizeof(uint64_t);
            // Each logical page holds 64 bytes headers. Therefore that size class won't fit logical page 
            
            while(true)
            {
                if(size_class > logical_page_size - sizeof(LogicalPageHeader) )
                {
                    logical_page_size <<= 1;
                }
                else
                {
                    break;
                }
            }

            // Arena params
            ArenaOptions arena_options;
            arena_options.cache_capacity = options.arena_initial_size;
            arena_options.page_alignment = logical_page_size;
            arena_options.use_huge_pages = options.use_huge_pages;
            arena_options.numa_node = options.numa_node;

            // Local heap params
            local_heap_params.size_class = size_class;
            local_heap_params.initial_size = options.local_pool_initial_size;
            local_heap_params.logical_page_size = logical_page_size;
            
            local_heap_params.segments_can_grow = options.local_pool_can_grow;
            local_heap_params.page_recycling_threshold = options.page_recycling_threshold;
            local_heap_params.grow_coefficient = options.grow_coefficient;
            
            local_heap_params.recyclable_deallocation_queue_size = options.recyclable_deallocation_queue_size;
            local_heap_params.non_recyclable_deallocation_queue_size = options.non_recyclable_deallocation_queue_size;
            local_heap_params.deallocation_queues_processing_threshold = options.deallocation_queues_processing_threshold; 

            // Central heap params
            central_heap_params.size_class = size_class;
            central_heap_params.initial_size = options.central_pool_initial_size;
            central_heap_params.logical_page_size = logical_page_size;
            
            central_heap_params.segments_can_grow = true;
            central_heap_params.page_recycling_threshold = options.page_recycling_threshold;
            central_heap_params.grow_coefficient = options.grow_coefficient;

            central_heap_params.recyclable_deallocation_queue_size = options.recyclable_deallocation_queue_size;
            central_heap_params.non_recyclable_deallocation_queue_size = options.non_recyclable_deallocation_queue_size;
            central_heap_params.deallocation_queues_processing_threshold = options.deallocation_queues_processing_threshold;            

            auto cached_thread_local_pool_count = options.thread_local_cached_heap_count;

            if(cached_thread_local_pool_count == 0 )
            {
                cached_thread_local_pool_count = static_cast<std::size_t>(ThreadUtilities::get_number_of_physical_cores());
            }

            ScalableMemoryPool::get_instance().set_thread_local_heap_cache_count(cached_thread_local_pool_count);
            return ScalableMemoryPool::get_instance().create(central_heap_params, local_heap_params, arena_options);
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate()
        {
            return ScalableMemoryPool::get_instance().allocate(sizeof(T));
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void deallocate(void*ptr)
        {
            if (unlikely(ptr == nullptr))
            {
                return;
            }

            ScalableMemoryPool::get_instance().deallocate(ptr);
        }
};

/*
    - std::allocator::allocate interface has to return contigious buffer for multiple objects which is not supported by HeapPool.
      ScalableMalloc have extra consideratinos for concurrency so not optimal for single threaded STL container allocators.
      Therefore SingleThreadedAllocator is the ideal allocator for STL containers
      
    - std::pmr::memory_resource may request arbitrary alignments which will most likely be 8 or 16 in libstdc++ and MSVC.
      However unlike ScalableMalloc, SingleThreadedAllocator supports only 16 byte alignments currently to keep its code simple.
      Therefore llmalloc::PMRResource is off by default. To enable it : -DENABLE_PMR/#define ENABLE_PMR
*/

struct SingleThreadedAllocatorOptions
{
    // SIZE AND CAPACITIES
    std::size_t arena_initial_size = 1024*1024*64;    // 64 MB
    std::size_t logical_page_counts_per_size_class[HeapPow2<>::BIN_COUNT] = {1,1,1,1,1,1,1,2,4,8,16,32,8,16,32};
    // RECYCLING & GROWING
    std::size_t page_recycling_threshold = 10;
    double grow_coefficient = 2;
    // DEALLOCATION QUEUES
    std::size_t deallocation_queue_processing_threshold = 409600;
    std::size_t deallocation_queue_size = 65536;
    // OTHERS
    bool use_huge_pages = false;
    int numa_node = -1;
    std::size_t non_small_objects_hash_map_size = 655360;
};

class SingleThreadedAllocator
{
    public:

        using HeapType = HeapPow2<>;
        using ArenaType = Arena;
        using HashmapType = Dictionary<uint64_t, std::size_t, typename ArenaType::MetadataAllocator>;

        static inline constexpr std::size_t MAX_SUPPORTED_ALIGNMENT = 16;

        FORCE_INLINE  static SingleThreadedAllocator& get_instance()
        {
            static SingleThreadedAllocator instance;
            return instance;
        }

        bool create(SingleThreadedAllocatorOptions options = SingleThreadedAllocatorOptions())
        {
            m_max_allocation_size = HeapType::get_max_allocation_size();
            m_max_small_object_size = HeapPow2<>::get_max_small_object_size();

            if( m_non_small_objects_hash_map.initialise( options.non_small_objects_hash_map_size / sizeof(typename HashmapType::DictionaryNode) ) == false)
            {
                return false;
            }

            typename HeapType::HeapCreationParams heap_params;
            heap_params.segments_can_grow = true;
            heap_params.non_recyclable_deallocation_queue_size = 0;

            heap_params.page_recycling_threshold_per_size_class = options.page_recycling_threshold;
            heap_params.segment_grow_coefficient = options.grow_coefficient;

            heap_params.deallocation_queues_processing_threshold = options.deallocation_queue_processing_threshold;
            heap_params.recyclable_deallocation_queue_size = options.deallocation_queue_size;

            for (std::size_t i = 0; i < HeapPow2<>::BIN_COUNT; i++)
            {
                heap_params.logical_page_counts[i] = options.logical_page_counts_per_size_class[i];
            }

            ArenaOptions arena_options;
            arena_options.cache_capacity = options.arena_initial_size;
            arena_options.use_huge_pages = options.use_huge_pages;
            arena_options.numa_node = options.numa_node;

            if(options.use_huge_pages == true)
            {
                std::size_t target_size = VirtualMemory::get_minimum_huge_page_size();

                heap_params.small_object_logical_page_size  = target_size;
                heap_params.medium_object_logical_page_size  = target_size;
                arena_options.page_alignment = target_size;
            }

            if(m_arena.create(arena_options) == false)
            {
                return false;
            }

            if(m_heap.create(heap_params, &m_arena) == false)
            {
                return false;
            }

            return true;
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size)
        {
            if (unlikely( size > m_max_allocation_size ))
            {
                return allocate_large_object(size);
            }

            void* ptr = m_heap.allocate(size);

            if(unlikely(size > m_max_small_object_size))
            {
                register_medium_object(ptr, size);
            }
    
            assert_msg(AlignmentAndSizeUtils::is_address_aligned(ptr, AlignmentAndSizeUtils::CPP_DEFAULT_ALLOCATION_ALIGNMENT), "Allocation address should be aligned to at least 16 bytes.");
            return ptr;
        }
        
        // Slow path removal function
        void* allocate_large_object(std::size_t size)
        {
            auto ptr = VirtualMemory::allocate(size, false);
            m_non_small_objects_hash_map.insert(reinterpret_cast<uint64_t>(ptr), size);
            assert_msg(AlignmentAndSizeUtils::is_address_aligned(ptr, AlignmentAndSizeUtils::CPP_DEFAULT_ALLOCATION_ALIGNMENT), "Allocation address should be aligned to at least 16 bytes.");
            return ptr;
        }

        // Slow path removal function
        void register_medium_object(void* ptr, std::size_t size)
        {
            m_non_small_objects_hash_map.insert(reinterpret_cast<uint64_t>(ptr), size);
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void deallocate(void*ptr)
        {
            std::size_t medium_or_large_size{0};

            if (unlikely( m_non_small_objects_hash_map.get( reinterpret_cast<uint64_t>(ptr), medium_or_large_size) ))
            {
                deallocate_medium_or_large_object(ptr, medium_or_large_size);
                return;
            }

            m_heap.deallocate(ptr, true);
        }

        // Slow path removal function
        void deallocate_medium_or_large_object(void* ptr, std::size_t medium_or_large_size)
        {
            if(medium_or_large_size < m_max_allocation_size)
            {
                m_heap.deallocate(ptr, false);
            }
            else
            {
                VirtualMemory::deallocate(ptr, medium_or_large_size);
            }
        }

    private:
        HeapType m_heap;
        HashmapType m_non_small_objects_hash_map;
        ArenaType m_arena;
        std::size_t m_max_allocation_size = 0;
        std::size_t m_max_small_object_size = 0;
};

/////////////////////////////////////////////////////////////
// std::allocator interface
template <class T>
class STLAllocator
{
    public:
        using value_type = T;

        STLAllocator() = default;

        template <class U>
        STLAllocator(const STLAllocator<U>&) {}

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        T* allocate(const std::size_t n)
        {
            T* ret = reinterpret_cast<T*>(SingleThreadedAllocator::get_instance().allocate(n * sizeof(T)));

            if (!ret) 
            {
                throw std::bad_alloc();
            }

            return ret;
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void deallocate(T* const p, const std::size_t n)
        {
            UNUSED(n);
            SingleThreadedAllocator::get_instance().deallocate(p);
        }

        template <class U>
        bool operator==(const STLAllocator<U>&) const noexcept
        {
            return true;
        }

        template <class U>
        bool operator!=(const STLAllocator<U>&) const noexcept
        {
            return false;
        }
};

/////////////////////////////////////////////////////////////
// std::pmr::memory_resource interface
#ifdef ENABLE_PMR
class PMRResource : public std::pmr::memory_resource 
{
    private : 

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* do_allocate(std::size_t bytes, std::size_t alignment) override 
        {
            assert_msg(alignment <= SingleThreadedAllocator::MAX_SUPPORTED_ALIGNMENT, "llmalloc::PMRResource supports alignments up to 16 bytes only."); // Debug mode check
            
            if(unlikely(alignment > SingleThreadedAllocator::MAX_SUPPORTED_ALIGNMENT)) // Release mode check
            {
                throw std::bad_alloc();
            }

            void* ptr = llmalloc::SingleThreadedAllocator::get_instance().allocate(bytes);

            if (!ptr) 
            {
                throw std::bad_alloc();
            }
            
            return ptr;
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override 
        {
            UNUSED(bytes);
            UNUSED(alignment);
            llmalloc::SingleThreadedAllocator::get_instance().deallocate(p);
        }

        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override 
        {
            return this == &other;
        }
};
#endif

struct ScalableMallocOptions
{
    // SIZE AND CAPACITIES
    std::size_t arena_initial_size = 2147483648;
    std::size_t central_logical_page_counts_per_size_class[HeapPow2<>::BIN_COUNT] = {1,1,1,1,1,1,1,2,4,8,16,32,8,16,32};
    std::size_t local_logical_page_counts_per_size_class[HeapPow2<>::BIN_COUNT] = {1,1,1,1,1,1,1,2,4,8,16,32,8,16,32};
    // RECYCLING & GROWING
    std::size_t page_recycling_threshold = 10;
    bool local_heaps_can_grow = true;
    double grow_coefficient = 2.0;
    // DEALLOCATION QUEUES
    std::size_t deallocation_queues_processing_threshold = 409600;
    std::size_t recyclable_deallocation_queue_size = 65536;
    std::size_t non_recyclable_deallocation_queue_size = 65536;
    // OTHERS
    bool use_huge_pages = false;
    int numa_node=-1;
    std::size_t thread_local_cached_heap_count = 0;
    #ifndef USE_ALLOC_HEADERS
    std::size_t non_small_and_aligned_objects_map_size = 655360; // Applies to no alloc headers
    #endif

    ScalableMallocOptions()
    {
        // SIZE AND CAPACITIES
        arena_initial_size = EnvironmentVariable::get_variable("llmalloc_arena_initial_size", arena_initial_size); // Default 2 GB      
        EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(local_logical_page_counts_per_size_class, HeapPow2<>::BIN_COUNT, EnvironmentVariable::get_variable("llmalloc_local_logical_page_counts_per_size_class", "1,1,1,1,1,1,1,2,4,8,16,32,8,16,32"));
        EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(central_logical_page_counts_per_size_class, HeapPow2<>::BIN_COUNT, EnvironmentVariable::get_variable("llmalloc_central_logical_page_counts_per_size_class", "1,1,1,1,1,1,1,2,4,8,16,32,8,16,32"));

        // RECYCLING & GROWING
        page_recycling_threshold = EnvironmentVariable::get_variable("llmalloc_page_recycling_threshold", page_recycling_threshold);
        grow_coefficient = EnvironmentVariable::get_variable("llmalloc_grow_coefficient", grow_coefficient);

        int numeric_local_heaps_can_grow = EnvironmentVariable::get_variable("llmalloc_local_heaps_can_grow", 1);
        local_heaps_can_grow = numeric_local_heaps_can_grow == 1 ? true : false;
        
        // DEALLOCATION QUEUES
        deallocation_queues_processing_threshold = EnvironmentVariable::get_variable("llmalloc_deallocation_queues_processing_threshold", deallocation_queues_processing_threshold);
        recyclable_deallocation_queue_size = EnvironmentVariable::get_variable("llmalloc_recyclable_deallocation_queue_size", recyclable_deallocation_queue_size);
        non_recyclable_deallocation_queue_size = EnvironmentVariable::get_variable("llmalloc_non_recyclable_deallocation_queue_size", non_recyclable_deallocation_queue_size);

        // OTHERS
        thread_local_cached_heap_count = EnvironmentVariable::get_variable("llmalloc_thread_local_cached_heap_count", thread_local_cached_heap_count);

        if(thread_local_cached_heap_count == 0 ) // If zero we will use physical core count
        {
            thread_local_cached_heap_count = static_cast<std::size_t>(ThreadUtilities::get_number_of_physical_cores());
        }

        int numeric_use_huge_pages = EnvironmentVariable::get_variable("llmalloc_use_huge_pages", 0);
        use_huge_pages = numeric_use_huge_pages == 1 ? true : false;

        numa_node = EnvironmentVariable::get_variable("llmalloc_numa_node", numa_node);

        #ifndef USE_ALLOC_HEADERS
        non_small_and_aligned_objects_map_size = EnvironmentVariable::get_variable("llmalloc_non_small_and_aligned_objects_map_size", non_small_and_aligned_objects_map_size);
        #endif
    }
};

PACKED
(
    struct AllocationMetadata
    {
        std::size_t size = 0;
        std::size_t padding_bytes = 0;
    }
);

class ScalableMalloc : public Lockable<LockPolicy::USERSPACE_LOCK>
{
    public:

        using ArenaType = Arena;
        using CentralHeapType = HeapPow2<MPMCBoundedQueue<uint64_t, typename ArenaType::MetadataAllocator>, LockPolicy::USERSPACE_LOCK>;
        using LocalHeapType = HeapPow2<BoundedQueue<uint64_t, typename ArenaType::MetadataAllocator>, LockPolicy::NO_LOCK>;
        using ScalableMallocType = ScalableAllocator<CentralHeapType, LocalHeapType>;
        using HashmapType = MPMCDictionary<uint64_t, AllocationMetadata, typename ArenaType::MetadataAllocator>;

        FORCE_INLINE static ScalableMalloc& get_instance()
        {
            static ScalableMalloc instance;
            return instance;
        }

        bool create(ScalableMallocOptions options = ScalableMallocOptions())
        {
            m_max_allocation_size = HeapPow2<>::get_max_allocation_size();
            m_max_small_object_size = HeapPow2<>::get_max_small_object_size();

            ///////////////////////////////////////////////////////////////
            // CREATE SCALABLE ALLOCATOR INSTANCE
            typename LocalHeapType::HeapCreationParams local_heap_params;
            local_heap_params.page_recycling_threshold_per_size_class = options.page_recycling_threshold;
            local_heap_params.segments_can_grow = options.local_heaps_can_grow;
            local_heap_params.segment_grow_coefficient = options.grow_coefficient;
            local_heap_params.deallocation_queues_processing_threshold = options.deallocation_queues_processing_threshold;
            local_heap_params.recyclable_deallocation_queue_size = options.recyclable_deallocation_queue_size;
            local_heap_params.non_recyclable_deallocation_queue_size = options.non_recyclable_deallocation_queue_size;

            for (std::size_t i = 0; i < HeapPow2<>::BIN_COUNT; i++)
            {
                local_heap_params.logical_page_counts[i] = options.local_logical_page_counts_per_size_class[i];
            }

            typename CentralHeapType::HeapCreationParams central_heap_params;
            central_heap_params.page_recycling_threshold_per_size_class = options.page_recycling_threshold;
            central_heap_params.segments_can_grow = true;
            central_heap_params.segment_grow_coefficient = options.grow_coefficient;
            central_heap_params.deallocation_queues_processing_threshold = options.deallocation_queues_processing_threshold;
            central_heap_params.recyclable_deallocation_queue_size = options.recyclable_deallocation_queue_size;
            central_heap_params.non_recyclable_deallocation_queue_size = options.non_recyclable_deallocation_queue_size;

            for (std::size_t i = 0; i < HeapPow2<>::BIN_COUNT; i++)
            {
                central_heap_params.logical_page_counts[i] = options.central_logical_page_counts_per_size_class[i];
            }

            ArenaOptions arena_options;
            arena_options.cache_capacity = options.arena_initial_size;
            arena_options.use_huge_pages = options.use_huge_pages;
            arena_options.numa_node = options.numa_node;
            
            if(options.use_huge_pages == true)
            {
                std::size_t target_size = VirtualMemory::get_minimum_huge_page_size();

                local_heap_params.small_object_logical_page_size = target_size;
                local_heap_params.medium_object_logical_page_size = target_size;

                central_heap_params.small_object_logical_page_size = target_size;
                central_heap_params.medium_object_logical_page_size = target_size;

                arena_options.page_alignment = target_size;
            }

            ScalableMallocType::get_instance().set_thread_local_heap_cache_count(options.thread_local_cached_heap_count);

            #ifndef USE_ALLOC_HEADERS
            m_small_object_logical_page_size = local_heap_params.small_object_logical_page_size;

            if( m_non_small_and_aligned_objects_map.initialise( options.non_small_and_aligned_objects_map_size / sizeof(typename HashmapType::DictionaryNode) ) == false)
            {
                return false;
            }
            #endif

            return ScalableMallocType::get_instance().create(central_heap_params, local_heap_params, arena_options);
        }

        #ifndef USE_ALLOC_HEADERS
        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size)
        {
            if (unlikely( size > m_max_allocation_size ))
            {
                return allocate_large_object(size);
            }

            void* ptr = ScalableMallocType::get_instance().allocate(size);

            if(unlikely(size > m_max_small_object_size))
            {
                register_unpadded_medium_object(ptr, size);
            }

            assert_msg(AlignmentAndSizeUtils::is_address_aligned(ptr, AlignmentAndSizeUtils::CPP_DEFAULT_ALLOCATION_ALIGNMENT), "Allocation address should be aligned to at least 16 bytes.");
            return ptr;
        }

        // Slow path removal function
        void* allocate_large_object(std::size_t size)
        {
            auto ptr = VirtualMemory::allocate(size, false);
            m_non_small_and_aligned_objects_map.insert(reinterpret_cast<uint64_t>(ptr), { size, 0 });
            assert_msg(AlignmentAndSizeUtils::is_address_aligned(ptr, AlignmentAndSizeUtils::CPP_DEFAULT_ALLOCATION_ALIGNMENT), "Allocation address should be aligned to at least 16 bytes.");
            return ptr;
        }

        // Slow path removal function
        void register_unpadded_medium_object(void* ptr, std::size_t size)
        {
            m_non_small_and_aligned_objects_map.insert(reinterpret_cast<uint64_t>(ptr), { size, 0 });
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void deallocate(void*ptr)
        {
            if (unlikely(ptr == nullptr))
            {
                return;
            }

            AllocationMetadata metadata;
            if (unlikely(m_non_small_and_aligned_objects_map.get(reinterpret_cast<uint64_t>(ptr), metadata)))
            {
                deallocate_non_small_or_aligned_object(metadata, ptr);
                return;
            }

            ScalableMallocType::get_instance().deallocate(ptr, true); // Small object without padding bytes
        }

        // Slow path removal function
        void deallocate_non_small_or_aligned_object(const AllocationMetadata& metadata, void* ptr)
        {
            uint64_t unpadded_pointer = reinterpret_cast<uint64_t>(ptr) - metadata.padding_bytes;

            if (metadata.size <= m_max_small_object_size)
            {
                ScalableMallocType::get_instance().deallocate(reinterpret_cast<void*>(unpadded_pointer), true); // Small object with padding bytes
            }
            else if (metadata.size <= m_max_allocation_size)
            {
                ScalableMallocType::get_instance().deallocate(reinterpret_cast<void*>(unpadded_pointer), false); // Medium object with or without padding bytes
            }
            else
            {
                VirtualMemory::deallocate(reinterpret_cast<void*>(unpadded_pointer), metadata.size); // Large object with or without padding bytes
            }
        }

        std::size_t get_usable_size(void* ptr)
        {
            AllocationMetadata metadata;

            if (unlikely(m_non_small_and_aligned_objects_map.get( reinterpret_cast<uint64_t>(ptr), metadata)))
            {
                return metadata.size;
            }

            // In case of a small object, we simply access to its page header to find its size quickly
            auto target_logical_page = Segment<LockPolicy::NO_LOCK>::get_logical_page_from_address(ptr, m_small_object_logical_page_size);
            auto size_class = target_logical_page->get_size_class();
            return size_class;
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void* allocate_aligned(std::size_t size, std::size_t alignment)
        {
            std::size_t adjusted_size = size + alignment; // Adding padding bytes
            
            if (unlikely( adjusted_size > m_max_allocation_size ))
            {
                return allocate_aligned_large_object(adjusted_size, alignment);
            }

            auto ptr = ScalableMallocType::get_instance().allocate(adjusted_size);

            std::size_t remainder = reinterpret_cast<std::uint64_t>(ptr) - ((reinterpret_cast<std::uint64_t>(ptr) / alignment) * alignment);
            std::size_t offset = alignment - remainder;

            void* ret = reinterpret_cast<void*>(reinterpret_cast<std::uint64_t>(ptr) + offset);

            m_non_small_and_aligned_objects_map.insert(reinterpret_cast<uint64_t>(ret), {adjusted_size , offset});

            assert_msg(AlignmentAndSizeUtils::is_address_aligned(ret, alignment), "Aligned allocation failed to meet the alignment requirement.");
            return ret;
        }

        // Slow path removal function
        void* allocate_aligned_large_object(std::size_t adjusted_size, std::size_t alignment)
        {
            auto ptr = VirtualMemory::allocate(adjusted_size, false);
            std::size_t remainder = reinterpret_cast<std::uint64_t>(ptr) - ((reinterpret_cast<std::uint64_t>(ptr) / alignment) * alignment);
            std::size_t offset = alignment - remainder;
            void* ret = reinterpret_cast<void*>(reinterpret_cast<std::uint64_t>(ptr) + offset);

            m_non_small_and_aligned_objects_map.insert(reinterpret_cast<uint64_t>(ret), { adjusted_size , offset });

            assert_msg(AlignmentAndSizeUtils::is_address_aligned(ret, alignment), "Aligned allocation failed to meet the alignment requirement.");

            return ret;
        }
        #else
        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size)
        {
            auto adjusted_size = size + sizeof(AllocationMetadata);

            if (unlikely( adjusted_size > m_max_allocation_size ))
            {
                return allocate_large_object(adjusted_size);
            }

            char* header_address = reinterpret_cast<char*>(ScalableMallocType::get_instance().allocate(adjusted_size));

            if(likely(header_address))
            {
                reinterpret_cast<AllocationMetadata*>(header_address)->size = adjusted_size;
                reinterpret_cast<AllocationMetadata*>(header_address)->padding_bytes = 0;

                assert_msg(AlignmentAndSizeUtils::is_address_aligned(header_address + sizeof(AllocationMetadata), AlignmentAndSizeUtils::CPP_DEFAULT_ALLOCATION_ALIGNMENT), "Allocation address should be aligned to at least 16 bytes.");
                return  header_address + sizeof(AllocationMetadata);
            }
            else
            {
                return nullptr;
            }
        }

        // Slow path removal function
        void* allocate_large_object(std::size_t adjusted_size)
        {
            auto header_address = reinterpret_cast<char*>(VirtualMemory::allocate(adjusted_size, false));
            if(likely(header_address))
            {
                reinterpret_cast<AllocationMetadata*>(header_address)->size = adjusted_size;
                reinterpret_cast<AllocationMetadata*>(header_address)->padding_bytes = 0;
                assert_msg(AlignmentAndSizeUtils::is_address_aligned(header_address + sizeof(AllocationMetadata), AlignmentAndSizeUtils::CPP_DEFAULT_ALLOCATION_ALIGNMENT), "Allocation address should be aligned to at least 16 bytes.");
                return header_address + sizeof(AllocationMetadata);
            }
            else
            {
                return nullptr;
            }
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void deallocate(void* ptr)
        {
            if (unlikely(ptr == nullptr))
            {
                return;
            }

            auto header_address = reinterpret_cast<char*>(ptr) - sizeof(AllocationMetadata);
            auto size = reinterpret_cast<AllocationMetadata*>(header_address)->size;
            auto orig_ptr = header_address - reinterpret_cast<AllocationMetadata*>(header_address)->padding_bytes;

            if(likely(size <= m_max_small_object_size))
            {
                ScalableMallocType::get_instance().deallocate(orig_ptr, true);
            }
            else if( size <= m_max_allocation_size)
            {
                ScalableMallocType::get_instance().deallocate(orig_ptr, false);
            }
            else
            {
                VirtualMemory::deallocate(reinterpret_cast<void*>(orig_ptr), size);
            }
        }

        std::size_t get_usable_size(void* ptr)
        {
            auto header_address = reinterpret_cast<char*>(ptr) - sizeof(AllocationMetadata);
            return reinterpret_cast<AllocationMetadata*>(header_address)->size - sizeof(AllocationMetadata);
        }

        ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        void* allocate_aligned(std::size_t size, std::size_t alignment)
        {
            std::size_t adjusted_size = size + sizeof(AllocationMetadata) + alignment; // Adding padding bytes

            if (unlikely( adjusted_size > m_max_allocation_size ))
            {
                return allocate_aligned_large_object(adjusted_size, alignment);
            }

            auto base = ScalableMallocType::get_instance().allocate(adjusted_size);

            if(likely(base))
            {
                uint64_t base_with_header = reinterpret_cast<std::uint64_t>(base) + sizeof(AllocationMetadata);
                std::size_t remainder = base_with_header - ( (base_with_header / alignment) * alignment);
                std::size_t offset = alignment - remainder;

                void* header_address = reinterpret_cast<void*>(reinterpret_cast<std::uint64_t>(base) + offset);
                void* ret = reinterpret_cast<void*>(reinterpret_cast<std::uint64_t>(header_address) + sizeof(AllocationMetadata));

                reinterpret_cast<AllocationMetadata*>(header_address)->size = adjusted_size;
                reinterpret_cast<AllocationMetadata*>(header_address)->padding_bytes = offset;

                assert_msg(AlignmentAndSizeUtils::is_address_aligned(ret, alignment), "Aligned allocation failed to meet the alignment requirement.");
                return ret;
            }
            else
            {
                return nullptr;
            }
        }

        // Slow path removal function
        void* allocate_aligned_large_object(std::size_t adjusted_size, std::size_t alignment)
        {
            auto base = VirtualMemory::allocate(adjusted_size, false);

            if(likely(base))
            {
                uint64_t base_with_header = reinterpret_cast<std::uint64_t>(base) + sizeof(AllocationMetadata);
                std::size_t remainder = base_with_header - ((base_with_header / alignment) * alignment);
                std::size_t offset = alignment - remainder;

                void* header_address = reinterpret_cast<void*>(reinterpret_cast<std::uint64_t>(base) + offset);
                void* ret = reinterpret_cast<void*>(reinterpret_cast<std::uint64_t>(header_address) + sizeof(AllocationMetadata));

                reinterpret_cast<AllocationMetadata*>(header_address)->size = adjusted_size;
                reinterpret_cast<AllocationMetadata*>(header_address)->padding_bytes = offset;

                assert_msg(AlignmentAndSizeUtils::is_address_aligned(ret, alignment), "Aligned allocation failed to meet the alignment requirement.");

                return ret;
            }
            else
            {
                return nullptr;
            }
        }
        #endif
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////
        // WRAPPER METHODS FOR MALLOC REPLACEMENT/INTEGRATION
        [[nodiscard]] void* operator_new(std::size_t size)
        {
            void* ret = allocate(size);

            if( unlikely(ret==nullptr) )
            {
                handle_operator_new_failure();
            }

            return ret;
        }

        void handle_operator_new_failure()
        {
            std::new_handler handler;

            this->enter_concurrent_context();
            ///////////////////////////////////////
            handler = std::get_new_handler();
            ///////////////////////////////////////
            this->leave_concurrent_context();

            if(handler != nullptr)
            {
                handler();
            }
            else
            {
                throw std::bad_alloc();
            }
        }

        [[nodiscard]] void* allocate_and_zero_memory(std::size_t num, std::size_t size)
        {       
            auto total_size = num * size;
            void* ret = allocate(total_size);

            if (ret != nullptr)
            {
                builtin_memset(ret, 0, total_size);
            }

            return ret;
        }

        [[nodiscard]] void* reallocate(void* ptr, std::size_t size)
        {
            if (ptr == nullptr)
            {
                return  allocate(size);
            }

            if (size == 0)
            {
                deallocate(ptr);
                return nullptr;
            }
            
            std::size_t old_size = get_usable_size(ptr);
            
            if(size <= old_size)
            {
                return ptr;
            }

            void* new_ptr = allocate(size);

            if (new_ptr != nullptr)
            {
                builtin_memcpy(new_ptr, ptr, old_size);
                deallocate(ptr);
            }

            return new_ptr;
        }

        [[nodiscard]]void* reallocate_and_zero_memory(void *ptr, std::size_t num, std::size_t size)
        {
            auto total_size = num*size;
            auto ret = reallocate(ptr, total_size);

            if(ret != nullptr)
            {
                builtin_memset(ret, 0, total_size);
            }

            return ret;
        }
        
        [[nodiscard]] void* operator_new_aligned(std::size_t size, std::size_t alignment)
        {
            void* ret = allocate_aligned(size, alignment);

            if( unlikely(ret==nullptr) )
            {
                handle_operator_new_failure();
            }

            return ret;
        }

        [[nodiscard]] void* aligned_reallocate(void* ptr, std::size_t size, std::size_t alignment)
        {
            if (ptr == nullptr)
            {
                return  allocate_aligned(size, alignment);
            }

            if (size == 0)
            {
                deallocate(ptr);
                return nullptr;
            }

            std::size_t old_size = get_usable_size(ptr);

            if(size <= old_size)
            {
                return ptr;
            }

            void* new_ptr = allocate_aligned(size, alignment);

            if (new_ptr != nullptr)
            {

                builtin_memcpy(new_ptr, ptr, old_size);
                deallocate(ptr);
            }

            return new_ptr;
        }
        ///////////////////////////////////////////////////////////////////////////////////////////////////////////

    private:
        #ifndef USE_ALLOC_HEADERS
        HashmapType m_non_small_and_aligned_objects_map;
        std::size_t m_small_object_logical_page_size = 0;
        #endif
        std::size_t m_max_allocation_size = 0;
        std::size_t m_max_small_object_size = 0;
};

} // NAMESPACE END 
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
ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_malloc(std::size_t size)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().allocate(size);
}

ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_aligned_malloc(std::size_t size, std::size_t alignment)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().allocate_aligned(size, alignment);
}

ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_operator_new(std::size_t size)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().operator_new(size);
}

ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_operator_new_aligned(std::size_t size, std::size_t alignment)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().operator_new_aligned(size, alignment);
}

ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void llmalloc_free(void* ptr)
{
    return llmalloc::ScalableMalloc::get_instance().deallocate(ptr);
}

ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_calloc(std::size_t num, std::size_t size)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().allocate_and_zero_memory(num, size);
}

ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
void* llmalloc_realloc(void* ptr, std::size_t size)
{
    #ifndef DISABLE_OVERRIDE_AUTO_INITIALISATIONS
    llmalloc_initialise();
    #endif
    return llmalloc::ScalableMalloc::get_instance().reallocate(ptr, size);
}

ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
std::size_t llmalloc_usable_size(void* ptr)
{
    return llmalloc::ScalableMalloc::get_instance().get_usable_size(ptr);
}

ALIGN_CODE(llmalloc::AlignmentConstants::CPU_CACHE_LINE_SIZE)
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
        builtin_memcpy(new_str, s, size);
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
        builtin_memcpy(new_str, s, size);
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
    UNUSED(alignment);
    llmalloc_free(ptr);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return llmalloc_operator_new_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    UNUSED(alignment);
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
    UNUSED(tag);
    return llmalloc_aligned_malloc(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    UNUSED(tag);
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
    UNUSED(tag);
    return llmalloc_aligned_malloc(size, alignment);
}

void* operator new[](std::size_t size, std::size_t alignment, const std::nothrow_t& tag) noexcept
{
    UNUSED(tag);
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
    UNUSED(size);
    llmalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept
{
    UNUSED(size);
    llmalloc_free(ptr);
}

void operator delete(void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    llmalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    llmalloc_free(ptr);
}

void operator delete(void* ptr, std::size_t size, std::size_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    llmalloc_free(ptr);
}

void operator delete[](void* ptr, std::size_t size, std::size_t align) noexcept
{
    UNUSED(size);
    UNUSED(align);
    llmalloc_free(ptr);
}

#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif