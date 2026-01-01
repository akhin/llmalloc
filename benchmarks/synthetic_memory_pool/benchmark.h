#ifndef __BENCHMARK_H__
#define __BENCHMARK_H__

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <array>
#include <thread>
#include <memory>
#include <string>
#include <fstream>

#include <benchmark_utilities.h>

#define CACHE_LINE_SIZE 64

struct alignas(CACHE_LINE_SIZE) Allocation
{
    alignas(CACHE_LINE_SIZE) void* ptr = nullptr;
    alignas(CACHE_LINE_SIZE) bool allocated = false;
    alignas(CACHE_LINE_SIZE) bool deallocated = false;
};

struct Foo
{
    uint64_t first;
    uint64_t second;
    uint64_t third;
    uint64_t fourth;
};

//////////////////////////////////////////////////
// BENCHMARK VARIABLES
static constexpr std::size_t INTERLEAVE_COUNT = 100;
static constexpr std::size_t ALLOCATION_COUNT_PER_PHASE = 100;
static constexpr std::size_t ALLOCATION_COUNT_PER_THREAD = ALLOCATION_COUNT_PER_PHASE * INTERLEAVE_COUNT;

static constexpr std::size_t MAX_THREAD_COUNT = 16; // Change accordingly
//////////////////////////////////////////////////
bool do_reads_writes_on_buffer(void* buffer, std::size_t buffer_size);

#define ENABLE_READS_AND_WRITES

using AllocationBucket = std::array<Allocation, ALLOCATION_COUNT_PER_THREAD>;
std::array<AllocationBucket, MAX_THREAD_COUNT> allocation_buckets;

template <typename AllocatorType>
void run_multithreaded_benchmark(int argc, char* argv[], std::string title, AllocatorType& allocator)
{
    std::size_t thread_count = 8;
    
    if(argc>1)
    {
        thread_count = std::stoi(argv[1]);
    }
    
    if(thread_count > MAX_THREAD_COUNT)
    {
        thread_count = MAX_THREAD_COUNT;
    }
    
    Stopwatch<StopwatchType::STOPWATCH_WITH_RDTSCP> stopwatch_duration;

    auto cpu_frequency = ProcessorUtilities::get_current_cpu_frequency_hertz();
    Console::print_colour(ConsoleColour::FG_YELLOW, "Current CPU frequency ( not min or max ) : " + std::to_string(cpu_frequency) + " Hz\n");

    Stopwatch<StopwatchType::STOPWATCH_WITH_RDTSCP> stopwatch;
    Statistics<double> reports_allocs[MAX_THREAD_COUNT];
    Statistics<double> reports_deallocs[MAX_THREAD_COUNT];
    
    //////////////////////////////////////////////////////////////////
    auto thread_function = [&](std::size_t allocation_bucket_index, std::size_t deallocation_bucket_index)
        {
            // warmup
            for (std::size_t i = 0; i < ALLOCATION_COUNT_PER_PHASE; i++)
            {
                auto warmup_ptr = allocator.allocate();
                
                if (warmup_ptr == nullptr)
                {
                    std::cout << "ALLOCATION FAILED\n";
                    assert(0 == 1);
                }
            }
        
            Console::print_colour(ConsoleColour::FG_YELLOW, "Sampling starting\n");
            std::size_t allocation_index = 0;

            stopwatch_duration.start();            

            for (std::size_t phase = 0; phase < INTERLEAVE_COUNT; phase++)
            {
                std::size_t current_phase_alloc_count = 0;
                std::size_t current_phase_dealloc_count = 0;

                // ALLOCATIONS
                for (std::size_t i = 0; i < ALLOCATION_COUNT_PER_PHASE; i++)
                {
                    void* ptr = nullptr;

                    //stopwatch.start();
                    ptr = allocator.allocate();
                    //stopwatch.stop();
                    //reports_allocs[allocation_bucket_index].add_sample(static_cast<double>(stopwatch.get_elapsed_cycles()));
                    
                    if (ptr == nullptr)
                    {
                        std::cout << "ALLOCATION FAILED\n";
                        assert(0 == 1);
                    }

                    #ifdef ENABLE_READS_AND_WRITES
                    if (!do_reads_writes_on_buffer(ptr, sizeof(Foo))) { Console::print_colour(ConsoleColour::FG_RED, "ALLOCATION FAILED !!!\n"); }
                    #endif

                    allocation_buckets[allocation_bucket_index][allocation_index].ptr = ptr;
                    allocation_buckets[allocation_bucket_index][allocation_index].allocated = true;

                    allocation_index++;
                    current_phase_alloc_count++;
                }
                
                while (true)
                {
                
                    for (std::size_t j = 0; j < current_phase_alloc_count; j++)
                    {
                        std::size_t current_dealloction_attempt_index = (ALLOCATION_COUNT_PER_PHASE * phase) + j;

                        if (allocation_buckets[deallocation_bucket_index][current_dealloction_attempt_index].allocated == true && allocation_buckets[deallocation_bucket_index][current_dealloction_attempt_index].deallocated == false)
                        {
                            //stopwatch.start();
                            allocator.deallocate(allocation_buckets[deallocation_bucket_index][current_dealloction_attempt_index].ptr);
                            //stopwatch.stop();
                            //reports_deallocs[allocation_bucket_index].add_sample(static_cast<double>(stopwatch.get_elapsed_cycles()));
                            
                            allocation_buckets[deallocation_bucket_index][current_dealloction_attempt_index].deallocated = true; // To avoid double-frees
                            current_phase_dealloc_count++;
                        }
                    }

                    if (current_phase_dealloc_count == current_phase_alloc_count)
                    {
                        stopwatch_duration.stop();            
                        break;
                    }
                }
                
            }
        };
    
    std::array<std::unique_ptr<std::thread>, MAX_THREAD_COUNT> threads;

    for (auto i{ 0 }; i < thread_count; i++)
    {
        threads[i] = std::unique_ptr<std::thread>(new std::thread(thread_function, i, thread_count - 1 - i));
    }

    std::size_t counter = 0;
    for (auto& thread : threads)
    {
        if(counter >= thread_count)
            break;

        thread->join();

        counter++;
    }
    
    counter = 0;
    for (auto& bucket : allocation_buckets)
    {
        if(counter >= thread_count)
            break;

        for (auto& allocation : bucket)
        {
            if (allocation.ptr)
            {
                if (allocation.allocated == false || allocation.deallocated == false)
                {
                    Console::print_colour(ConsoleColour::FG_RED, "TEST FAILED !!!\n");
                }
            }
        }
        
        counter++;
    }
    //////////////////////////////////////////////////////////////////
    /*
    for(std::size_t i = 0 ; i<thread_count;i++)
    {
        reports_allocs[i].print("Allocations , thread" + std::to_string(i)  + " : " + title, "clock cycles");
    }
    
    for(std::size_t i = 0 ; i<thread_count;i++)
    {
        reports_deallocs[i].print("Deallocations , thread" + std::to_string(i)  + " : " + title, "clock cycles");
    }
    */
    Console::print_colour(ConsoleColour::FG_BLUE, "Clock cycles : " + std::to_string(stopwatch_duration.get_elapsed_cycles() ) + "\n");
    Console::print_colour(ConsoleColour::FG_BLUE, "Number of threads : " + std::to_string(thread_count) + "\n");
    Console::print_colour(ConsoleColour::FG_BLUE, "Number of allocs and frees per thread : " + std::to_string(ALLOCATION_COUNT_PER_THREAD) + "\n");
    Console::print_colour(ConsoleColour::FG_BLUE, "Interleave count : " + std::to_string(INTERLEAVE_COUNT) + "\n");
    
    std::ofstream outfile("samples.txt", std::ios_base::app);
    outfile << stopwatch_duration.get_elapsed_cycles() << std::endl;
    outfile.close();

    #ifdef _WIN32
    std::system("pause");
    #endif
}

bool do_reads_writes_on_buffer(void* buffer, std::size_t buffer_size)
{
    if (buffer == nullptr)
    {
        return false;
    }

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

#endif