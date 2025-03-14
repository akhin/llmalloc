#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include <vector>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../include/utilities/alignment_and_size_utils.h"
#include "../../include/arena.h"

using namespace std;

UnitTest unit_test;


inline bool validate_buffer(void* buffer, std::size_t buffer_size)
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

int main(int argc, char* argv[])
{
    // ARENA BASIC
    {
        Arena arena;
        ArenaOptions options;
        options.cache_capacity = 655360;
        options.page_alignment = 65536;

        bool success = arena.create(options);
        if (!success) { std::cout << "ARENA CREATION FAILED !!!" << std::endl; return -1; }
        vector<uint64_t> page_addresses;

        for (std::size_t i = 0; i < 32; i++)
        {
            auto ptr = arena.allocate(65536);

            if (ptr == nullptr)
            {
                std::cout << "ALLOCATION FAILED !!!" << std::endl;
                return -1;
            }
        }

        for (const auto& address : page_addresses)
        {
            void* ptr = reinterpret_cast<void*>(address);
            arena.release_to_system(ptr, 65536);
        }
    }

    // ARENA ALIGNMENTS TO PAGE SIZE OR MULTIPLE OF PAGE SIZE
    {
        vector<size_t> page_alignments;

        #if _WIN32
        page_alignments.push_back(65536);
        page_alignments.push_back(65536 * 2);
        page_alignments.push_back(65536 * 4);
        page_alignments.push_back(65536 * 8);
        #elif __linux__
        page_alignments.push_back(4096);
        page_alignments.push_back(4096 * 2);
        page_alignments.push_back(4096 * 4);
        page_alignments.push_back(4096 * 8);
        page_alignments.push_back(65536);
        page_alignments.push_back(65536 * 2);
        page_alignments.push_back(65536 * 4);
        page_alignments.push_back(65536 * 8);
        #endif

        for (const auto page_alignment : page_alignments)
        {
            Arena arena;
            ArenaOptions options;
            options.cache_capacity = 4096 * 64;
            options.page_alignment = page_alignment;
            bool success = arena.create(options);

            unit_test.test_equals(success, true, "arena", "arena creation with alignment " + std::to_string(page_alignment));

            auto ptr = arena.allocate(page_alignment);

            if (ptr == nullptr)
            {
                std::cout << "ALLOCATION FAILED !!!" << std::endl;
                return -1;
            }

            if (validate_buffer(ptr, page_alignment) == false)
            {
                std::cout << "BUFFER VALIDATION FAILED !!!" << std::endl;
                return -1;
            }

            unit_test.test_equals(AlignmentAndSizeUtils::is_address_aligned(ptr, page_alignment), true, "arena", "arena alignment validation : " + std::to_string(page_alignment));

            arena.release_to_system(ptr, page_alignment);
        }
    }

    // HUGE PAGES
    {
        // CHECK IF WE CAN USE HUGE PAGE IN THE TEST
        if (VirtualMemory::is_huge_page_available())
        {
            auto min_huge_page_size = VirtualMemory::get_minimum_huge_page_size();

            std::cout << "Minimum huge page size on the system : " << min_huge_page_size << endl;

            void* test_ptr = VirtualMemory::allocate(min_huge_page_size, true, false);

            unit_test.test_equals(test_ptr != nullptr, true, "system virtual memory", "huge page allocation");

            if (test_ptr == nullptr) { return -1;}

            VirtualMemory::deallocate(test_ptr, min_huge_page_size);

            Arena arena;
            ArenaOptions options;
            options.cache_capacity = min_huge_page_size * 2;
            options.page_alignment = min_huge_page_size;
            options.use_huge_pages = true;
            bool success = arena.create(options);
            if (!success) { std::cout << "HUGE PAGE ARENA CREATION FAILED !!!" << std::endl; return -1; }

            auto ptr = arena.allocate(min_huge_page_size);

            unit_test.test_equals(validate_buffer(ptr, min_huge_page_size), true, "arena", "huge page");

            if (ptr) { arena.release_to_system(ptr, min_huge_page_size); }
        }
        else
        {
            std::cout << "Huge page not setup on system , will not continue." << std::endl;
        }
    }

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("Arena");
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