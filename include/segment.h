/*
    - A SEGMENT IS A COLLECTION OF LOGICAL PAGES. IT ALLOWS TO GROW IN SIZE AND TO RETURN UNUSED LOGICAL PAGES BACK TO THE SYSTEM

    - IT WILL PLACE A LOGICAL PAGE HEADER TO INITIAL 64 BYTES OF EVERY LOGICAL PAGE.            
     
    ! IMPORTANT : THE EXTERNAL BUFFER SHOULD BE ALIGNED TO LOGICAL PAGE SIZE. THAT IS CRITICAL FOR REACHING LOGICAL PAGE HEADERS
*/
#ifndef _SEGMENT_H_
#define _SEGMENT_H_

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "compiler/hints_branch_predictor.h"
#include "compiler/hints_hot_code.h"
#include "compiler/unused.h"

#include "cpu/alignment_constants.h"
#include "os/assert_msg.h"

#include "utilities/alignment_and_size_utils.h"
#include "utilities/lockable.h"

#include "arena.h"
#include "logical_page_header.h"
#include "logical_page.h"

struct SegmentCreationParameters
{
    std::size_t m_logical_page_size = 0;
    std::size_t m_logical_page_count = 0;
    std::size_t m_page_recycling_threshold = 0;
    uint32_t m_size_class = 0;
    double m_grow_coefficient = 2.0; // 0 means that we will be growing by allocating only required amount. Applies to segments that can grow
    bool m_can_grow = true;
};

#if defined(ENABLE_PERF_TRACES) // VOLTRON_EXCLUDE
#include <cstdio>
#endif // VOLTRON_EXCLUDE

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

#endif