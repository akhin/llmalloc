/*
    - IT IS A FIRST-IN-LAST-OUT FREELIST IMPLEMENTATION. IT CAN HOLD ONLY ONE SIZE CLASS.

    - IF THE PASSED BUFFER IS START OF A VIRTUAL PAGE AND THE PASSED SIZE IS A VM PAGE SIZE , THEN IT WILL BE CORRESPONDING TO AN ACTUAL VM PAGE
      IDEAL USE CASE IS ITS CORRESPONDING TO A VM PAGE / BEING VM PAGE ALIGNED. SO THAT A SINGLE PAYLOAD WILL NOT SPREAD TO DIFFERENT VM PAGES.
*/
#ifndef _LOGICAL_PAGE_H_
#define _LOGICAL_PAGE_H_

#include <cstddef>
#include <cstdint>
#include "compiler/unused.h"
#include "compiler/packed.h"
#include "compiler/hints_hot_code.h"
#include "compiler/hints_branch_predictor.h"
#include "cpu/alignment_constants.h"
#include "os/assert_msg.h"
#include "utilities/alignment_and_size_utils.h"

#include "logical_page_header.h"

#ifdef UNIT_TEST // VOLTRON_EXCLUDE
#include <string>
#endif // VOLTRON_EXCLUDE

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

#endif