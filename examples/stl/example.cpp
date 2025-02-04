/* 
    llmalloc::PMRResource is off by default since it supports only 16 byte alignments for simplicity
    LibStdC++ and MSVC requests mostly 8/16 byte alignment. But enable it for your app only after testing in your environment
*/
#define ENABLE_PMR 

#include <llmalloc.h>
#include <cstddef>
#include <vector>

#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;

int main()
{
    if (llmalloc::SingleThreadedAllocator::get_instance().create() == false)
    {
        return -1;
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    // std::allocator interface
    {
        auto start_time = high_resolution_clock::now();

        std::vector<std::size_t, llmalloc::STLAllocator<std::size_t>> v;

        for (std::size_t i = 0; i < 4096; i++)
        {
            v.push_back(i);
        }

        auto end_time = high_resolution_clock::now();
        auto duration_ns = duration_cast<nanoseconds>(end_time - start_time).count();
        std::cout << "llmalloc::STLAllocator execution time: " << duration_ns << " nanoseconds" << std::endl;
    }

    {
        auto start_time = high_resolution_clock::now();

        std::vector<std::size_t> v;

        for (std::size_t i = 0; i < 4096; i++)
        {
            v.push_back(i);
        }

        auto end_time = high_resolution_clock::now();
        auto duration_ns = duration_cast<nanoseconds>(end_time - start_time).count();
        std::cout << "std::allocator execution time: " << duration_ns << " nanoseconds" << std::endl;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    // std::pmr::memory_resource interface
    #ifdef ENABLE_PMR
    {
        auto start_time = high_resolution_clock::now();

        llmalloc::PMRResource llmalloc_pmr_resource;
        std::pmr::polymorphic_allocator<std::size_t> llmalloc_polymorphic_allocator(&llmalloc_pmr_resource);
        std::pmr::vector<std::size_t> v(llmalloc_polymorphic_allocator);

        for (std::size_t i = 0; i < 4096; i++)
        {
            v.push_back(i);
        }

        auto end_time = high_resolution_clock::now();
        auto duration_ns = duration_cast<nanoseconds>(end_time - start_time).count();
        std::cout << "llmalloc pmr resource execution time: " << duration_ns << " nanoseconds" << std::endl;
    }

    {
        auto start_time = high_resolution_clock::now();

        std::pmr::monotonic_buffer_resource std_pmr_resource;
        std::pmr::polymorphic_allocator<std::size_t> std_polymorphic_allocator(&std_pmr_resource);
        std::pmr::vector<std::size_t> v(std_polymorphic_allocator);

        for (std::size_t i = 0; i < 4096; i++)
        {
            v.push_back(i);
        }

        auto end_time = high_resolution_clock::now();
        auto duration_ns = duration_cast<nanoseconds>(end_time - start_time).count();
        std::cout << "std monotonic_buffer_resource execution time: " << duration_ns << " nanoseconds" << std::endl;
    }

    {
        auto start_time = high_resolution_clock::now();

        std::pmr::unsynchronized_pool_resource std_pmr_resource;
        std::pmr::polymorphic_allocator<std::size_t> std_polymorphic_allocator(&std_pmr_resource);
        std::pmr::vector<std::size_t> v(std_polymorphic_allocator);

        for (std::size_t i = 0; i < 4096; i++)
        {
            v.push_back(i);
        }

        auto end_time = high_resolution_clock::now();
        auto duration_ns = duration_cast<nanoseconds>(end_time - start_time).count();
        std::cout << "std unsynchronized_pool_resource execution time: " << duration_ns << " nanoseconds" << std::endl;
    }
    #endif

    return 0;
}