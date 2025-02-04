#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include <iostream>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <thread>
#include <mutex>
#include <string>
using namespace std;

#include "../../include/arena.h"
#include "../../include/logical_page.h"
#include "../../include/os/thread_utilities.h"

static UnitTest unit_test;

template <typename LogicalPageType>
void test_incorrect_creation(std::size_t buffer_size);

template <typename LogicalPageType>
bool test_exhaustion(std::size_t buffer_size, std::size_t allocation_count, std::size_t allocation_size);

template <typename LogicalPageType>
bool test_general(std::size_t buffer_size);

bool validate_buffer(void* buffer, std::size_t buffer_size);

int main(int argc, char* argv[])
{
    // LOGICAL PAGE HEADER SIZE
    unit_test.test_equals(sizeof(LogicalPageHeader), 64, "Logical page header size" , "Should be 64 bytes");

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // LOGICAL PAGE TESTS
    {
        // CREATION CHECKS
        constexpr std::size_t small_buffer_size = 6;
        char small_buffer[small_buffer_size];
        LogicalPage logical_page;
        bool success = logical_page.create(small_buffer, small_buffer_size, 8);
        unit_test.test_equals(false, success, "creation checks", "too small buffer size");

        std::size_t allocation_size = 128;
        // EXHAUSTION TEST
        test_exhaustion<LogicalPage>(allocation_size * 32, 32, allocation_size); // 4KB page , Doesnt use any allocation header,
        test_exhaustion<LogicalPage>(allocation_size * 512, 512, allocation_size); //64kb page Doesnt use any allocation header,

        // GENERAL TESTS
        test_general<LogicalPage>(65536);
    }

    //// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("Logical page");
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

template <typename LogicalPageType>
bool test_exhaustion(std::size_t buffer_size, std::size_t allocation_count, std::size_t allocation_size)
{
    Arena arena;
    LogicalPageType logical_page;

    bool success = logical_page.create(arena.allocate(buffer_size), buffer_size, static_cast<uint16_t>(allocation_size));

    if (success == false)
    {
        return false;
    }

    std::vector<std::size_t> pointers;

    // WE ALLOCATE ALL THE BUFFER
    for (std::size_t i{ 0 }; i < allocation_count; i++)
    {
        auto ptr = logical_page.allocate(allocation_size);
        if (ptr)
        {
            pointers.push_back(reinterpret_cast<std::size_t>(ptr));
        }
    }

    unit_test.test_equals(logical_page.get_used_size(), buffer_size, "logical page exhaustion", "Verify that the page exhausted");

    auto first_node = reinterpret_cast<void*>(pointers[0]);
    pointers.erase(pointers.begin() + 0);

    logical_page.deallocate(first_node);
    unit_test.test_equals(false, logical_page.get_head_node() == nullptr, "logical page exhaustion", "The head node should not be empty after exhaustion");

    // Deallocations
    for (auto& pointer : pointers)
    {
        logical_page.deallocate(reinterpret_cast<void*>(pointer));
    }

    unit_test.test_equals(logical_page.get_used_size(), 0, "logical page exhaustion", "Verify that the page is fully available");

    return true;
}

template <typename LogicalPageType>
bool test_general(std::size_t buffer_size)
{
    Arena arena;
    std::size_t BUFFER_SIZE = buffer_size;

    LogicalPageType logical_page;

    bool success = logical_page.create(arena.allocate(BUFFER_SIZE), BUFFER_SIZE, 128);

    std::string test_category = "allocation general ";

    unit_test.test_equals(true, success, test_category, "logical_page creation");

    std::vector<uintptr_t> pointers;
    std::vector<std::size_t> increment_sizes;
    std::size_t allocation_size = 12;
    std::size_t last_used_size = 0;

    std::size_t counter = 0;

    while (true)
    {
        auto ptr = logical_page.allocate(allocation_size);

        if (ptr == nullptr)
        {
            if (counter == 0)
            {
                return false;
            }
            else
            {
                break;
            }
        }

        increment_sizes.push_back(logical_page.get_used_size() - last_used_size);

        pointers.push_back(reinterpret_cast<uintptr_t>(ptr));

        allocation_size++;
        last_used_size = logical_page.get_used_size();
        counter++;
    }

    counter = 0;

    for (auto ptr : pointers)
    {
        logical_page.deallocate(reinterpret_cast<void*>(ptr));

        unit_test.test_equals(logical_page.get_used_size(), last_used_size - increment_sizes[counter], test_category, "deallocation " + std::to_string(counter));

        last_used_size = logical_page.get_used_size();
        counter++;
    }

    unit_test.test_equals(logical_page.get_used_size(), 0, test_category, "cumulative deallocations ");
    return true;
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