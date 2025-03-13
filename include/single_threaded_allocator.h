/*
    - std::allocator::allocate interface has to return contigious buffer for multiple objects which is not supported by HeapPool.
      ScalableMalloc have extra consideratinos for concurrency so not optimal for single threaded STL container allocators.
      Therefore SingleThreadedAllocator is the ideal allocator for STL containers
      
    - std::pmr::memory_resource may request arbitrary alignments which will most likely be 8 or 16 in libstdc++ and MSVC.
      However unlike ScalableMalloc, SingleThreadedAllocator supports only 16 byte alignments currently to keep its code simple.
      Therefore llmalloc::PMRResource is off by default. To enable it : -DENABLE_PMR/#define ENABLE_PMR
*/
#ifndef _SINGLE_THREADED_ALLOCATOR_H_
#define _SINGLE_THREADED_ALLOCATOR_H_

#include <cstdint>
#include <cstddef>
#include <new>

#ifdef ENABLE_PMR // VOLTRON_EXCLUDE
#include <memory_resource>
#endif // VOLTRON_EXCLUDE

#include "compiler/unused.h"
#include "compiler/hints_hot_code.h"
#include "compiler/hints_branch_predictor.h"

#include "cpu/alignment_constants.h"
#include "os/virtual_memory.h"
#include "os/assert_msg.h"

#include "utilities/dictionary.h"
#include "utilities/alignment_and_size_utils.h"

#include "arena.h"
#include "heap_pow2.h"

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
    std::size_t deallocation_queue_sizes[HeapPow2<>::BIN_COUNT] = { 65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536 };
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

            heap_params.page_recycling_threshold_per_size_class = options.page_recycling_threshold;
            heap_params.segment_grow_coefficient = options.grow_coefficient;

            heap_params.deallocation_queues_processing_threshold = options.deallocation_queue_processing_threshold;
            
            
            for (std::size_t i = 0; i < HeapPow2<>::BIN_COUNT; i++)
            {
                heap_params.logical_page_counts[i] = options.logical_page_counts_per_size_class[i];
                heap_params.non_recyclable_deallocation_queue_sizes[i] = 0;
                heap_params.recyclable_deallocation_queue_sizes[i] = options.deallocation_queue_sizes[i];
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

#endif