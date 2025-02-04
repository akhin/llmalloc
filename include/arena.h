/*
    - IT RELEASES ONLY UNUSED PAGES. RELEASING USED PAGES IS UP TO THE CALLERS.

    - IF HUGE PAGE IS SPECIFIED AND IF THAT HUGE PAGE ALLOCATION FAILS, WE WILL FAILOVER TO A REGULAR PAGE ALLOCATION
    
    - CAN BE NUMA AWARE IF SPEFICIED

    - LINUX ALLOCATION GRANULARITY IS 4KB (4096) , OTH IT IS 64KB ( 16 * 4096 ) ON WINDOWS .
      REGARDING WINDOWS PAGE ALLOCATION GRANULARITY : https://devblogs.microsoft.com/oldnewthing/20031008-00/?p=42223
*/
#ifndef _ARENA_H_
#define _ARENA_H_

#include <cstddef>
#include <cstdint>

#include "compiler/unused.h"

#include "os/assert_msg.h"
#include "os/virtual_memory.h"

#include "utilities/lockable.h"
#include "utilities/alignment_and_size_utils.h"

#ifdef UNIT_TEST // VOLTRON_EXCLUDE
#include <string>
#endif // VOLTRON_EXCLUDE

#ifdef ENABLE_PERF_TRACES // VOLTRON_EXCLUDE
#include <cstdio>
#endif // VOLTRON_EXCLUDE

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

#endif