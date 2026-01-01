#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include "../../include/arena.h"
#include "../../include/segment.h"

#include <iostream>
#include <cstring>
#include <cstddef>
#include <cstdlib>
#include <vector>

using namespace std;

UnitTest unit_test;

struct Allocation
{
    void* ptr = nullptr;
    uint16_t size_class = 0;
};

bool validate_buffer(void* buffer, std::size_t buffer_size);

template <typename LogicalPageType>
bool run_test(const std::string name, std::size_t logical_page_buffer_size, uint16_t size_class, std::size_t logical_page_count, std::size_t allocation_count, bool is_anysize);

int main(int argc, char* argv[])
{
    /////////////////////////////////////////////////////////////////////////////////
    std::size_t logical_page_count_per_segment = 32;
    std::size_t constexpr size_class = 128;

    //////////////////////////////////////////////////////////////////////////
    // UNBOUNDED SEGMENT TESTS, LOGICAL PAGE SIZE 4K, WE DON'T SUPPORT THIS ON WINDOWS SO ONLY LINUX
    // LogicalPageHeader is normally 32 bytes. However if UNIT_TEST defined, logical pages declare 2 more variables , therefore 48 bytes
    #ifdef __linux__
    run_test<LogicalPage>("LogicalPage", 4096, size_class, logical_page_count_per_segment, 992, false); // no padding bytes , 64 bytes page header so 4096-40=4032 , 4032/128=31 , 31*32 = 992
    #endif

    //////////////////////////////////////////////////////////////////////////
    // UNBOUNDED SEGMENT TESTS, LOGICAL PAGE SIZE 64K
    run_test<LogicalPage>("LogicalPage", 65536, size_class, logical_page_count_per_segment, 16352, false); // no padding bytes , 64 bytes page header so 65536-64=65472 , 65472/128=511 , 511*32 = 16352

    //////////////////////////////////////////////////////////////////////////
    // BOUNDED SEGMENT TEST ( THREAD LOCAL )
    {
        Arena  arena;
        ArenaOptions options;
        options.cache_capacity = 65536 * 10;
        options.page_alignment = 65536;

        bool success = arena.create(options);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

        Segment<LockPolicy::NO_LOCK> segment;
        std::vector<std::uint64_t> pointers;

        char* initial_buffer = static_cast <char*>(arena.allocate(65536));

        SegmentCreationParameters params;
        params.m_size_class = 2048;
        params.m_logical_page_count = 1;
        params.m_logical_page_size = 65536;
        params.m_page_recycling_threshold = 1;

        params.m_can_grow = false;

        success = segment.create(initial_buffer, &arena, params); // We start with 1 logical page , and threshold is also 1
        if (!success) { std::cout << "Segment creation failed"; return -1; }

        // Logical page actual capacity 65536-40 = 65496 , 65496/2038 = 31 objects
        for (std::size_t i = 0; i < 31; i++)
        {
            auto ptr = segment.allocate(2048);
            pointers.push_back(reinterpret_cast<std::uint64_t>(ptr));
        }

        unit_test.test_equals(segment.get_logical_page_count(), 1, "bounded segment", "exhaustion");

        auto ptr = segment.allocate(2048); // Attempt to grow
        if (ptr) { std::cout << "Bounded segment failed. It should have not grown" << std::endl; return -1; }
        unit_test.test_equals(segment.get_logical_page_count(), 1, "bounded segment", "allocation after exhaustion");

        for (const auto& ptr : pointers)
        {
            segment.deallocate(reinterpret_cast<void*>(ptr));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // PAGE RECYCLING
    {
        Arena  arena;
        ArenaOptions options;
        options.cache_capacity = 65536 * 10;
        options.page_alignment = 65536;
        bool success = arena.create(options);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

        Segment<LockPolicy::NO_LOCK> segment;
        std::vector<std::uint64_t> pointers;

        char* initial_buffer = static_cast <char*>(arena.allocate(65536));

        SegmentCreationParameters params;
        params.m_size_class = 2048;
        params.m_logical_page_count = 1;
        params.m_logical_page_size = 65536;
        params.m_page_recycling_threshold = 1;
        params.m_grow_coefficient = 0;

        success = segment.create(initial_buffer , &arena, params); // We start with 1 logical page , and threshold is also 1
        if (!success) { std::cout << "Segment creation failed"; return -1; }

        // Logical page actual capacity 65536-64 = 65472 , 65496/2048 = 31 objects
        for (std::size_t i = 0; i < 31; i++)
        {
            auto ptr = segment.allocate(2048);
            pointers.push_back(reinterpret_cast<std::uint64_t>(ptr));
        }

        unit_test.test_equals(segment.get_logical_page_count(), 1, "segment", "exhaustion");

        auto ptr = segment.allocate(2048); // Segment will grow by 1 page
        unit_test.test_equals(segment.get_logical_page_count(), 2, "segment", "allocation after exhaustion");

        segment.deallocate(ptr); // INVOKING PAGE RECYCLING
        unit_test.test_equals(segment.get_logical_page_count(), 1, "segment", "allocation after recycling");

        for (const auto& ptr : pointers)
        {
            segment.deallocate(reinterpret_cast<void*>(ptr));
        }
    }

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("Segment");
    std::cout.flush();
    
    #if _WIN32
    bool pause = true;
    if(argc > 1)
    {
        if (std::strcmp(argv[1], "no_pause") == 0)
            pause = false;
    }
    if(pause)
        std::system("pause");
    #endif

    return unit_test.did_all_pass();
}

bool validate_buffer(void* buffer, std::size_t buffer_size)
{
    char* char_buffer = static_cast<char*>(buffer);

    // TRY WRITING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        char* dest = char_buffer + i;
        *dest = static_cast<char>(i);
    }

    // NOW CHECK READING
    for (std::size_t i = 0; i < buffer_size; i++)
    {
        auto test = char_buffer[i];
        if (test != static_cast<char>(i))
        {
            return false;
        }
    }

    return true;
}

template <typename LogicalPageType>
bool run_test(const std::string name, std::size_t logical_page_buffer_size, uint16_t size_class, std::size_t logical_page_count, std::size_t allocation_count, bool is_anysize)
{
    //////////////////////////////////////
    // CHECK CREATION FAILURES
    {
        Segment<LockPolicy::NO_LOCK> segment;
        Arena arena;
        ArenaOptions options;
        options.cache_capacity = 65536 * 10;
        options.page_alignment = logical_page_buffer_size;
        bool success = arena.create(options);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

        SegmentCreationParameters params;
        params.m_size_class = 0;
        params.m_logical_page_count = 0;
        params.m_logical_page_size = logical_page_buffer_size;
        params.m_page_recycling_threshold = 0;

        success = segment.create(nullptr , &arena, params);
        unit_test.test_equals(success, false, "creation checks " + name, "invalid segment creation argument");
    }
    //////////////////////////////////////
    Segment<LockPolicy::NO_LOCK> segment;
    Arena arena;
    ArenaOptions options;
    options.cache_capacity = 65536 * 10;
    options.page_alignment = logical_page_buffer_size;
    bool success = arena.create(options);
    if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return false; }

    char* initial_buffer = static_cast <char*>(arena.allocate(logical_page_count* logical_page_buffer_size));

    SegmentCreationParameters params;
    params.m_size_class = size_class;
    params.m_logical_page_count = logical_page_count;
    params.m_logical_page_size = logical_page_buffer_size;
    params.m_page_recycling_threshold = logical_page_count * 2;
    params.m_grow_coefficient = 1;

    success = segment.create(initial_buffer , &arena, params);
    unit_test.test_equals(true, success, "creation checks " + name, "positive creation case");

    std::vector<Allocation> allocations;

    // WE ALLOCATE ALL THE BUFFER
    for (std::size_t i{ 0 }; i < allocation_count; i++)
    {
        auto ptr = segment.allocate(size_class);
        if (ptr)
        {
            Allocation allocation;
            allocation.size_class = size_class;
            allocation.ptr = ptr;
            allocations.push_back(allocation);

            
            bool is_page_header_good = size_class == Segment<LockPolicy::NO_LOCK>::get_size_class_from_address(ptr, logical_page_buffer_size);

            if (is_page_header_good == false)
            {
                std::cout << "PAGE HEADER VALIDATION FAILED" << std::endl;
                return false;

            }
            
        }
        else
        {
            std::cout << "ALLOCATION FAILED" << std::endl;
            return false;
        }
    }

    std::cout << std::endl << std::endl << "VALIDATING THE ALL ALLOCATED BUFFERS" << std::endl << std::endl;

    for (auto& allocation : allocations)
    {
        bool buffer_ok = validate_buffer(reinterpret_cast<void*>(allocation.ptr), size_class);
        if (buffer_ok == false)
        {
            std::cout << "BUFFER VALIDATION FAILED" << std::endl;
            return false;
        }
    }

    unit_test.test_equals(allocations.size(), allocation_count, "segment allocation " + name, "number of successful allocations");

    if (is_anysize)
    {
        auto ptr1 = reinterpret_cast<void*>(allocations[0].ptr);
        auto ptr2 = reinterpret_cast<void*>(allocations[1].ptr);
        auto ptr3 = reinterpret_cast<void*>(allocations[2].ptr);

        segment.deallocate(ptr1);
        segment.deallocate(ptr2);
        segment.deallocate(ptr3);

        allocations.erase(allocations.begin() + 0);
        allocations.erase(allocations.begin() + 1);
        allocations.erase(allocations.begin() + 2);


        auto new_ptr1 = segment.allocate(size_class);
        auto new_ptr2 = segment.allocate(size_class);
        auto new_ptr3 = segment.allocate(size_class);

        LLMALLOC_UNUSED(new_ptr1);
        LLMALLOC_UNUSED(new_ptr2);
        LLMALLOC_UNUSED(new_ptr3);
    }

    // 1 MORE ALLOCATION TO TRIGGER GROW
    auto current_logical_page_count = segment.get_logical_page_count();
    unit_test.test_equals(logical_page_count, current_logical_page_count, name + " segment allocation", "logical page count before grow");

    auto latest_ptr = segment.allocate(size_class);
    LLMALLOC_UNUSED(latest_ptr);

    current_logical_page_count = segment.get_logical_page_count();
    unit_test.test_equals(logical_page_count * 2, current_logical_page_count, name + " segment allocation", "logical page count after grow");

    // Deallocate all the rest
    for (auto& allocation : allocations)
    {
        auto size_class = Segment<LockPolicy::NO_LOCK>::get_size_class_from_address(allocation.ptr, logical_page_buffer_size);

        if (size_class != allocation.size_class)
        {
            std::cout << "PAGE HEADER VALIDATION FAILED : FOUND SIZECLASS IS WRONG !!!" << std::endl;
            return false;
        }
        

        segment.deallocate(reinterpret_cast<void*>(allocation.ptr));
    }

    return true;
}