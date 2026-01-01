#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "compiler/hints_hot_code.h"
#include "compiler/builtin_functions.h"

#include "cpu/alignment_constants.h"
#include "os/assert_msg.h"

#include "utilities/bounded_queue.h"
#include "utilities/lockable.h"
#include "utilities/alignment_and_size_utils.h"

#include "arena.h"
#include "segment.h"

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
            std::size_t recyclable_deallocation_queue_sizes[BIN_COUNT] = { 65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536 };
            std::size_t non_recyclable_deallocation_queue_sizes[BIN_COUNT] = { 65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536 };
        };

        [[nodiscard]] bool create(const HeapCreationParams& params, ArenaType* arena)
        {
            //////////////////////////////////////////////////////////////////////////////////////////////
            // 1. CHECKS
            llmalloc_assert_msg(arena, "Heap must receive a valid arena instance.");

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
            llmalloc_assert_msg(AlignmentAndSizeUtils::is_address_page_allocation_granularity_aligned(reinterpret_cast<void*>(small_objects_buffer_address)), "HeapPow2: Arena failed to pass an address which is aligned to OS page allocation granularity.");

            char* medium_objects_buffer_address = reinterpret_cast<char*>(arena->allocate_aligned(medium_objects_required_buffer_size, m_medium_object_logical_page_size));
            llmalloc_assert_msg(AlignmentAndSizeUtils::is_address_page_allocation_granularity_aligned(reinterpret_cast<void*>(medium_objects_buffer_address)), "HeapPow2: Arena failed to pass an address which is aligned to OS page allocation granularity.");
            llmalloc_assert_msg(AlignmentAndSizeUtils::is_address_aligned(reinterpret_cast<void*>(medium_objects_buffer_address), m_medium_object_logical_page_size), "HeapPow2: Failed to get an address which is aligned to medium objects page size.");

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
                if( params.non_recyclable_deallocation_queue_sizes[i] > 0 )
                {
                    if (m_non_recyclable_deallocation_queues[i].create(params.non_recyclable_deallocation_queue_sizes[i]) == false)
                    {
                        return false;
                    }
                }

                if (m_recyclable_deallocation_queues[i].create(params.recyclable_deallocation_queue_sizes[i]) == false)
                {
                    return false;
                }
            }

            return true;
        }

        LLMALLOC_ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE) [[nodiscard]]
        void* allocate(std::size_t size = 0)
        {
            size = size < MIN_SIZE_CLASS ? MIN_SIZE_CLASS : size;
            size = get_first_pow2_of(size);
            auto bin_index = get_pow2_bin_index_from_size(size);

            m_potential_pending_max_deallocation_count++;

            if (llmalloc_unlikely(m_potential_pending_max_deallocation_count >= m_deallocation_queue_processing_threshold))
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
        LLMALLOC_ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
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

        LLMALLOC_ALIGN_CODE(AlignmentConstants::CPU_CACHE_LINE_SIZE)
        bool deallocate(void* ptr, bool is_small_object)
        {
            auto logical_page_size = is_small_object ? m_small_object_logical_page_size : m_medium_object_logical_page_size;
            auto target_logical_page = SegmentType::get_logical_page_from_address(ptr, logical_page_size);

            auto size_class = target_logical_page->get_size_class();

            llmalloc_assert_msg(size_class >= MIN_SIZE_CLASS, "HeapPow2 deallocate : Found size class is invalid. The pointer may not have been allocated by this allocator.");
            
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
                    if (llmalloc_likely(ret != nullptr))
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
        LLMALLOC_FORCE_INLINE std::size_t get_first_pow2_of(std::size_t input)
        {
            assert(input>= MIN_SIZE_CLASS);

            /* 
            No need for the if check below , as the caller will pass a miniumum of MIN_SIZE_CLASS which is > 1
            if (llmalloc_unlikely(input <= 1))
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
        LLMALLOC_FORCE_INLINE std::size_t get_pow2_bin_index_from_size(std::size_t size)
        {
            std::size_t index = static_cast<std::size_t>(63 - llmalloc_builtin_clzl(static_cast<unsigned long>(size))) - LOG2_MIN_SIZE_CLASS;
            index = index > MAX_BIN_INDEX ? MAX_BIN_INDEX : index;
            return index;
        }
};