/*
    POD LOGICAL PAGE HEADER
    LOGICAL PAGE HEADERS WILL BE PLACED TO THE FIRST 64 BYTES OF EVERY LOGICAL PAGE

    PAHOLE OUTPUT :
                            size: 64, cachelines: 1, members: 10
                            last cacheline: 64 bytes
*/
#ifndef _LOGICAL_PAGE_HEADER_H_
#define _LOGICAL_PAGE_HEADER_H_

#include <cstdint>
#include "compiler/packed.h"

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

#endif