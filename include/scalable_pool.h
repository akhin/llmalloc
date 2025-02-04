// INTERFACE WRAPPER FOR THREAD CACHING MEMORY POOL
#ifndef _SCALABLE_POOL_H_
#define _SCALABLE_POOL_H_

#include <cstddef>

#include "compiler/hints_hot_code.h"
#include "compiler/hints_branch_predictor.h"
#include "cpu/alignment_constants.h"
#include "os/thread_utilities.h"
#include "os/virtual_memory.h"

#include "utilities/lockable.h"
#include "utilities/bounded_queue.h"
#include "utilities/mpmc_queue.h"

#include "arena.h"
#include "logical_page_header.h"
#include "heap_pool.h"
#include "scalable_allocator.h"

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

#endif