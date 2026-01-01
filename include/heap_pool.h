#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>

#include "compiler/hot_code.h"
#include "compiler/unused.h"

#include "cpu/alignment_constants.h"
#include "os/assert_msg.h"

#include "utilities/bounded_queue.h"
#include "utilities/lockable.h"
#include "utilities/alignment_and_size_utils.h"

#include "arena.h"
#include "segment.h"

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
            llmalloc_assert_msg(params.size_class > 0, "Pool size class should be greater than zero.");
            llmalloc_assert_msg(params.initial_size > 0, "Pool initial size should be greater than zero.");
            llmalloc_assert_msg(params.initial_size % params.logical_page_size == 0, "Initial pool size should be a multiple of its logical page size.");

            m_arena = arena_ptr;

            auto buffer_length = params.initial_size;
            auto buffer_address = reinterpret_cast<uint64_t>(m_arena->allocate(buffer_length));

            llmalloc_assert_msg(AlignmentAndSizeUtils::is_address_page_allocation_granularity_aligned(reinterpret_cast<void*>(buffer_address)), "Arena failed to return page alloc granularity aligned address for memory pool.");
            
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

        LLMALLOC_ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size = 0)
        {
            LLMALLOC_UNUSED(size);

            m_potential_pending_max_deallocation_count++;

            if(llmalloc_unlikely(m_potential_pending_max_deallocation_count.load() >= m_deallocation_queue_processing_threshold ))
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
        LLMALLOC_ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
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

        LLMALLOC_ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        bool deallocate(void* ptr, bool is_small_object = false)
        {
            LLMALLOC_UNUSED(is_small_object);

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
            llmalloc_assert_msg(bin_index>0, "HeapPool holds only a single segment.");
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
                    if (llmalloc_likely(ret != nullptr))
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