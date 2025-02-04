#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include "../../include/compiler/unused.h"
#include "../../include/utilities/mpmc_bounded_queue.h"

#include <cstdlib>
#include <cstring>
#include <atomic>
#include <vector>
#include <thread>
#include <iostream>
using namespace std;

UnitTest unit_test;

class QueueAllocator
{
public:
    static void* allocate(std::size_t size)
    {
        #ifdef __linux__
        return std::aligned_alloc(64, size);
        #elif _WIN32    
        return _aligned_malloc(size, 64);
        #endif
    }

    static void deallocate(void* ptr, std::size_t size)
    {
        UNUSED(size);
        #ifdef __linux__
        free(ptr);
        #elif _WIN32    
        _aligned_free(ptr);
        #endif
    }
};

int main(int argc, char* argv[])
{
    // SINGLE THREAD  BASIC OPERATIONS SINGLE PAGE
    {
        MPMCBoundedQueue<uint64_t, QueueAllocator> q;

        if (q.create(20000 * 8) == false)
        {
            return -1;
        }

        std::size_t pointer_count = 20000;

        std::vector<uint64_t> pointers;

        for (std::size_t i = 0; i < pointer_count; i++)
        {
            void* ptr = malloc(8);
            auto ptr_value = reinterpret_cast<uint64_t>(ptr);

            pointers.push_back(ptr_value);
            q.push(ptr_value);
        }

        std::size_t remaining = pointer_count;
        std::size_t counter = 0;

        while (true)
        {
            uint64_t pointer = 0;

            if (q.try_pop(pointer) == true)
            {
                remaining--;

                if (pointer != pointers[counter])
                {
                    std::cout << "TEST FAILED !!!" << std::endl;
                    return -1;
                }

                counter++;
            }

            if (remaining == 0)
            {
                break;
            }
        }

        for (auto& ptr : pointers) { std::free(reinterpret_cast<void*>(ptr)); }
    }
    
    // CONCURRENCY TESTS
    {
        MPMCBoundedQueue<uint64_t, QueueAllocator> q;
        
        constexpr std::size_t producer_thread_count = 128;
        constexpr std::size_t production_count_per_producer_thread = 640;
        
        auto buffer_size = producer_thread_count * production_count_per_producer_thread * sizeof(uint64_t);

        if (q.create(buffer_size) == false)
        {
            return -1;
        }

        std::vector<std::unique_ptr<std::thread>> producer_threads;
        std::thread* consumer_thread;

        std::size_t consumer_result = 0; // Will only be accessed by consumer thread
        std::atomic<bool> producers_finished;
        producers_finished.store(false);

        auto thread_function = [&](bool is_consumer = false)
        {
            if (is_consumer)
            {
                std::size_t remaining = producer_thread_count * production_count_per_producer_thread;
                while (true)
                {
                    uint64_t pointer = 0;

                    if (q.try_pop(pointer) == true)
                    {
                        consumer_result++;
                        remaining--;
                    }

                    if (remaining == 0)
                    {
                        return;
                    }
                }
            }
            else
            {
                // produce
                for (std::size_t i = 0; i < production_count_per_producer_thread; i++)
                {
                    auto ptr = malloc(8);
                    q.push( reinterpret_cast<uint64_t>(ptr));
                    ConcurrencyTestUtilities::sleep_randomly_usecs(5000);
                }
            }
        };

        // PRODUCER THREADS
        for (std::size_t i{ 0 }; i < producer_thread_count; i++)
        {
            producer_threads.emplace_back(new std::thread(thread_function));
        }

        // CONSUMER THREAD
        consumer_thread = new std::thread(thread_function, true);

        // WAIT TILL PRODUCERS ARE DONE
        for (auto& thread : producer_threads)
        {
            thread->join();
        }

        // LET CONSUMER KNOW THAT PRODUCERS ARE  DONE
        producers_finished.store(true);
        // WAIT FOR CONSUMER THREAD
        consumer_thread->join();

        unit_test.test_equals(consumer_result, producer_thread_count * production_count_per_producer_thread, "mpmc_bounded_queue", "thread safety");

        delete consumer_thread;
    }
    
    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("mpmc_bounded_queue");
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