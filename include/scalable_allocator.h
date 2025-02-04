/*
    - THE PASSED ALLOCATOR WILL HAVE A CENTRAL HEAP AND ALSO THREAD LOCAL HEAPS.

    - ALLOCATIONS INITIALLY WILL BE FROM LOCAL ( EITHER THREAD LOCAL ) HEAPS. IF LOCAL HEAPS ARE EXHAUSTED , THEN CENTRAL HEAP WILL BE USED.

    - USES CONFIGURABLE METADATA ( DEFAULT 256KB ) TO STORE THREAD LOCAL HEAPS. ALSO INITIALLY USES 64KB METADATA TO STORE THE CENTRAL HEAP
*/
#ifndef _SCALABLE_ALLOCATOR_H_
#define _SCALABLE_ALLOCATOR_H_

#include <atomic>
#include <cstddef>
#include <type_traits>

#include "compiler/hints_hot_code.h"
#include "compiler/hints_branch_predictor.h"

#include "cpu/alignment_constants.h"
#include "os/thread_local_storage.h"

#include "utilities/alignment_and_size_utils.h"
#include "utilities/lockable.h"

#include "arena.h"

#ifdef ENABLE_PERF_TRACES // VOLTRON_EXCLUDE
#include <cstdio>
#endif // VOLTRON_EXCLUDE

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

#endif