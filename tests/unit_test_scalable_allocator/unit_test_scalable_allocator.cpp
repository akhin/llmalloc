#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro
#include "../../include/os/thread_utilities.h"

#include "../../llmalloc.h"
using namespace llmalloc;

#include <array>
#include <cstddef>
#include <cstring>
#include <thread>
#include <vector>
#include <mutex>
#include <iostream>
using namespace std;

UnitTest unit_test;

struct Allocation
{
    void* ptr = nullptr;
    uint16_t size_class = 0;
    bool allocated = false;
};

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

std::mutex g_mutex; // For shared allocations vector

template <typename AllocatorType>
bool validate_allocation(std::size_t allocation_size, std::vector<Allocation>& allocation_vector, std::size_t repeat_count = 1)
{
    for (std::size_t i = 0; i < repeat_count; i++)
    {
        void* ptr = nullptr;

        ptr = AllocatorType::get_instance().allocate(allocation_size);

        if (ptr == nullptr)
        {
            std::cout << "ALLOCATION FAILED !!!" << std::endl;
            return false;
        }
        else
        {
            bool buffer_ok = validate_buffer(reinterpret_cast<void*>(ptr), allocation_size);

            if (buffer_ok == false)
            {
                std::cout << "BUFFER VALIDATION FAILED !!!" << std::endl;
                return false;

            }
            Allocation allocation;
            allocation.ptr = ptr;
            allocation.allocated = true;
            allocation.size_class = static_cast<uint16_t>(allocation_size);

            g_mutex.lock();
            allocation_vector.push_back(allocation);
            g_mutex.unlock();
        }
    }

    return true;
}

using LocalHeapType = HeapPow2<>;
using CentralHeapType = HeapPow2<MPMCBoundedQueue<uint64_t, typename Arena::MetadataAllocator>, LockPolicy::USERSPACE_LOCK>;

using PerThreadCachingAllocatorType = ScalableAllocator<
    CentralHeapType,
    LocalHeapType
>;

int main(int argc, char* argv[])
{
    bool success = false;

    ////////////////////////////////////////////////////////////////////////////
    // PER THREAD
    {
        auto allocation_size = 16;

        typename LocalHeapType::HeapCreationParams local_heap_params;

        typename CentralHeapType::HeapCreationParams central_heap_params;

        ArenaOptions options;
        options.cache_capacity = 6553600;
        options.page_alignment = 65536;

        PerThreadCachingAllocatorType::get_instance().set_thread_local_heap_cache_count(8);

        success = PerThreadCachingAllocatorType::get_instance().create(central_heap_params, local_heap_params, options, 262144);
        
        if (!success) { std::cout << "per thread caching allocator creation failed !!!" << std::endl; return -1; }

        constexpr std::size_t thread_count = 32;
        constexpr std::size_t allocation_per_thread_count = 64;

        if (PerThreadCachingAllocatorType::get_instance().get_max_thread_local_heap_count() < thread_count)
        {
            std::cout << "Configured thread count can't be handled. If this was working before , that means sizeof your heap ( and segments ) increased !!\n";
            return -1;
        }

        using AllocationBucket = std::array<Allocation, allocation_per_thread_count>;
        std::array<AllocationBucket, thread_count> allocation_buckets;

        auto allocating_thread_function = [&](std::size_t allocation_bucket_index, std::size_t deallocation_bucket_index)
        {
            ConcurrencyTestUtilities::pin_calling_thread_randomly(8);

            ConcurrencyTestUtilities::set_calling_thread_name("THREAD_" + std::to_string(allocation_bucket_index));

            // ALLOCATIONS
            for (std::size_t i{ 0 }; i < allocation_per_thread_count; i++)
            {
                void* ptr = nullptr;
                ptr = PerThreadCachingAllocatorType::get_instance().allocate(allocation_size);

                if (ptr == nullptr) { std::cout << "ALLOCATION FAILED !!!" << std::endl; return false; }
                if (!validate_buffer(ptr, allocation_size)) { Console::console_output_with_colour(ConsoleColour::FG_RED, "ALLOCATION FAILED !!!\n"); }

                Allocation current_allocation;
                current_allocation.ptr = ptr;
                current_allocation.allocated = true;
                current_allocation.size_class = static_cast<uint16_t>(allocation_size);

                allocation_buckets[allocation_bucket_index][i] = current_allocation;
                ConcurrencyTestUtilities::sleep_randomly_usecs(3000);
            }

            // DEALLOCATIONS
            auto remaining_deallocations = allocation_per_thread_count;
            while (true)
            {
                for (std::size_t i = 0; i < allocation_per_thread_count; i++)
                {
                    auto target_deallocation = &(allocation_buckets[deallocation_bucket_index][i]);
                    if (target_deallocation->allocated == true)
                    {
                        if (target_deallocation->ptr != nullptr)
                        {
                            PerThreadCachingAllocatorType::get_instance().deallocate(target_deallocation->ptr);
                            target_deallocation->ptr = nullptr; // To avoid double-frees
                            remaining_deallocations--;
                        }
                    }

                    ConcurrencyTestUtilities::sleep_randomly_usecs(3000);
                }

                if (remaining_deallocations == 0)
                {
                    break;
                }
            };

            return true;
        };

        std::vector<std::unique_ptr<std::thread>> allocating_threads;
        for (std::size_t i{ 0 }; i < thread_count; i++)
        {
            allocating_threads.emplace_back(new std::thread(allocating_thread_function, i, thread_count - 1 - i));
        }

        for (auto& thread : allocating_threads)
        {
            thread->join();
        }

        std::size_t total_allocated_size{ 0 };

        for (auto& bucket : allocation_buckets)
        {
            for (auto& allocation : bucket)
            {
                total_allocated_size += allocation.size_class;
                if (allocation.allocated == false || allocation.ptr != nullptr) { Console::console_output_with_colour(ConsoleColour::FG_RED, "TEST FAILED !!!\n"); }
            }
        }

        unit_test.test_equals(total_allocated_size, allocation_size * allocation_per_thread_count * thread_count, "scalable allocator", "per thread caching");

        unit_test.test_equals(PerThreadCachingAllocatorType::get_instance().get_observed_unique_thread_count(), thread_count, "scalable allocator", "per thread caching - observed unique thread count");
    }

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("ScalableAllocator");
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