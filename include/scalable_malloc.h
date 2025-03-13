#ifndef _SCALABLE_MALLOC_H_
#define _SCALABLE_MALLOC_H_

#include "compiler/hints_hot_code.h"
#include "compiler/hints_branch_predictor.h"
#include "compiler/builtin_functions.h"
#include "compiler/packed.h"

#include "cpu/alignment_constants.h"

#include "os/assert_msg.h"
#include "os/thread_utilities.h"
#include "os/virtual_memory.h"
#include "os/environment_variable.h"

#include "utilities/alignment_and_size_utils.h"
#include "utilities/bounded_queue.h"
#include "utilities/mpmc_bounded_queue.h"
#include "utilities/mpmc_dictionary.h"
#include "utilities/userspace_spinlock.h"
#include "utilities/lockable.h"

#include "arena.h"
#include "heap_pow2.h"
#include "scalable_allocator.h"

#include <array>
#include <cstddef>
#include <new>

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
    std::size_t recyclable_deallocation_queue_sizes[HeapPow2<>::BIN_COUNT] = {65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536};
    std::size_t non_recyclable_deallocation_queue_sizes[HeapPow2<>::BIN_COUNT] = {65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536};
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
        
        EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(recyclable_deallocation_queue_sizes, HeapPow2<>::BIN_COUNT, EnvironmentVariable::get_variable("llmalloc_recyclable_deallocation_queue_sizes", "65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536"));
        EnvironmentVariable::set_numeric_array_from_comma_separated_value_string(non_recyclable_deallocation_queue_sizes, HeapPow2<>::BIN_COUNT, EnvironmentVariable::get_variable("llmalloc_non_recyclable_deallocation_queue_sizes", "65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536,65536"));
        
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

            for (std::size_t i = 0; i < HeapPow2<>::BIN_COUNT; i++)
            {
                local_heap_params.logical_page_counts[i] = options.local_logical_page_counts_per_size_class[i];
                local_heap_params.recyclable_deallocation_queue_sizes[i] = options.recyclable_deallocation_queue_sizes[i];
                local_heap_params.non_recyclable_deallocation_queue_sizes[i] = options.non_recyclable_deallocation_queue_sizes[i];
            }

            typename CentralHeapType::HeapCreationParams central_heap_params;
            central_heap_params.page_recycling_threshold_per_size_class = options.page_recycling_threshold;
            central_heap_params.segments_can_grow = true;
            central_heap_params.segment_grow_coefficient = options.grow_coefficient;
            central_heap_params.deallocation_queues_processing_threshold = options.deallocation_queues_processing_threshold;

            for (std::size_t i = 0; i < HeapPow2<>::BIN_COUNT; i++)
            {
                central_heap_params.logical_page_counts[i] = options.central_logical_page_counts_per_size_class[i];
                central_heap_params.recyclable_deallocation_queue_sizes[i] = options.recyclable_deallocation_queue_sizes[i];
                central_heap_params.non_recyclable_deallocation_queue_sizes[i] = options.non_recyclable_deallocation_queue_sizes[i];
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

#endif