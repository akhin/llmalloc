#ifndef _ALIGNMENT_AND_SIZE_UTILS_H_
#define _ALIGNMENT_AND_SIZE_UTILS_H_

#include <cassert>
#include <cstdint>
#include <cstddef>
#include "../os/virtual_memory.h"

class AlignmentAndSizeUtils
{
    public:
    
        static constexpr inline std::size_t CPP_DEFAULT_ALLOCATION_ALIGNMENT = 16;

        // Generic check including non pow2
        static bool is_address_aligned(void* address, std::size_t alignment)
        {
            auto address_in_question = reinterpret_cast<uint64_t>( address );
            auto remainder = address_in_question - (address_in_question / alignment) * alignment;
            return remainder == 0;
        }

        static bool is_pow2(std::size_t size)
        {
            return size > 0 && (size & (size - 1)) == 0;
        }

        // Page allocation granularity is always pow2
        static bool is_address_page_allocation_granularity_aligned(void* address)
        {
            assert(is_pow2(VirtualMemory::PAGE_ALLOCATION_GRANULARITY));
            return (reinterpret_cast<uint64_t>(address) & (VirtualMemory::PAGE_ALLOCATION_GRANULARITY - 1)) == 0;
        }

        // Generic check including non pow2
        static bool is_size_a_multiple_of_page_allocation_granularity(std::size_t input)
        {
            auto remainder = input - (input / VirtualMemory::PAGE_ALLOCATION_GRANULARITY) * VirtualMemory::PAGE_ALLOCATION_GRANULARITY;
            return remainder == 0;
        }
        
        static std::size_t get_next_pow2_multiple_of(std::size_t input, std::size_t multiple)
        {
            // Not checking if the given input is already a multiple
            return ((input + multiple - 1) & ~(multiple - 1));
        }
};

#endif