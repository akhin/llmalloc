/*
    MULTITHREADED BENCHMARK CREATES MULTIPLE THREADS WHERE EACH THREAD MAKES ALLOCATIONS
    ONCE THREADS COMPLETE THEIR ALLOCATIONS , THEY START TO DEALLOCATE POINTERS WHICH WERE ALLOCATED BY DIFFERENT THREADS.
*/

#ifndef __BENCHMARK_H__
#define __BENCHMARK_H__

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <array>
#include <thread>
#include <memory>
#include <string>
#include <algorithm>
#include <random>
#include "../benchmark_utilities.h"

#define CACHE_LINE_SIZE 64

struct alignas(CACHE_LINE_SIZE) Allocation
{
    alignas(CACHE_LINE_SIZE) std::size_t size_class = 0;
    alignas(CACHE_LINE_SIZE) void* ptr = nullptr;
    alignas(CACHE_LINE_SIZE) bool allocated = false;
    alignas(CACHE_LINE_SIZE) bool deallocated = false;
};

//////////////////////////////////////////////////
// BENCHMARK VARIABLES
static constexpr bool SHUFFLE = false;
static constexpr std::size_t INTERLEAVE_COUNT = 1000;
static constexpr std::size_t SIZE_CLASS_COUNT = 12;
static constexpr std::size_t SCALE = 50;
static constexpr std::size_t SIZE_CLASSES[SIZE_CLASS_COUNT] = { 16,32,64,128,256,512,1024,2048,4096,8192,16384,32768};
static constexpr std::size_t ALLOCATION_COUNTS_FOR_SIZE_CLASSES_PER_PHASE[SIZE_CLASS_COUNT] = { 4089*SCALE/ INTERLEAVE_COUNT,2044*SCALE/ INTERLEAVE_COUNT,1022*SCALE/INTERLEAVE_COUNT,511*SCALE/INTERLEAVE_COUNT,255*SCALE/INTERLEAVE_COUNT,127*SCALE/INTERLEAVE_COUNT,63*SCALE/ INTERLEAVE_COUNT,31*SCALE/ INTERLEAVE_COUNT, 31*SCALE/ INTERLEAVE_COUNT, 31*SCALE/ INTERLEAVE_COUNT,31*SCALE/ INTERLEAVE_COUNT, 31*SCALE/ INTERLEAVE_COUNT };

constexpr std::size_t sum_allocation_counts(const std::size_t* arr, std::size_t size) 
{
    std::size_t total = 0;
    for (std::size_t i = 0; i < size; ++i) 
    {
        total += arr[i];
    }
    return total;
}

static constexpr std::size_t TOTAL_ALLOCATION_COUNT_PER_PHASE = sum_allocation_counts(ALLOCATION_COUNTS_FOR_SIZE_CLASSES_PER_PHASE, SIZE_CLASS_COUNT);

static constexpr std::size_t TOTAL_MAX_ALLOCATIONS_PER_THREAD = 8266*SCALE; // It is max when interleave count is 1 , but can change with higher interleave counts

// THREAD COUNTS ARE BASED ON MY CURRENT TARGET DEVICES PHYSICAL CORE COUNTS. CHANGE ACCORDINGLY IN YOUR CASE.

static constexpr std::size_t MAX_THREAD_COUNT = 16;
//////////////////////////////////////////////////
bool prepare_benchmark_data(std::size_t thread_count);
void run_multithreaded_benchmark(std::size_t thread_count, const char* samples_output_file);
bool do_reads_writes_on_buffer(void* buffer, std::size_t buffer_size);

#define ENABLE_READS_AND_WRITES

using AllocationBucket = std::array<Allocation, TOTAL_MAX_ALLOCATIONS_PER_THREAD>;
std::array<AllocationBucket, MAX_THREAD_COUNT> allocation_buckets;

void run_multithreaded_benchmark(std::size_t thread_count, const char* samples_output_file="samples.txt")
{
    Stopwatch<StopwatchType::STOPWATCH_WITH_RDTSCP> stopwatch;

    auto cpu_frequency = ProcessorUtilities::get_current_cpu_frequency_hertz();
    Console::print_colour(ConsoleColour::FG_YELLOW, "Current CPU frequency ( not min or max ) : " + std::to_string(cpu_frequency) + " Hz\n" );

    stopwatch.start();
    //////////////////////////////////////////////////////////////////
    auto thread_function = [&](std::size_t allocation_bucket_index, std::size_t deallocation_bucket_index)
    {
        // Initialise the allocator 
        auto init_ptr = malloc(32);
        free(init_ptr);
        
        std::size_t allocation_index = 0;
        
        for(std::size_t phase = 0; phase<INTERLEAVE_COUNT; phase++)
        {
            std::size_t current_phase_alloc_count = 0;
            std::size_t current_phase_dealloc_count = 0;

            // ALLOCATIONS
            for (std::size_t i = 0; i < TOTAL_ALLOCATION_COUNT_PER_PHASE; i++)
            {
                void* ptr = nullptr;
                std::size_t allocation_size = allocation_buckets[allocation_bucket_index][allocation_bucket_index].size_class;

                if(allocation_size == 0) 
                { 
                    Console::print_colour(ConsoleColour::FG_RED, "Invalid sizeclass, fix prepare_data function !!!\n"); 
                }

                ptr = malloc(allocation_size);

                #ifdef ENABLE_READS_AND_WRITES
                if (!do_reads_writes_on_buffer(ptr, allocation_size)) { Console::print_colour(ConsoleColour::FG_RED, "ALLOCATION FAILED !!!\n"); }
                #endif

                allocation_buckets[allocation_bucket_index][allocation_index].ptr = ptr;
                allocation_buckets[allocation_bucket_index][allocation_index].allocated = true;

                allocation_index++;
                current_phase_alloc_count++;
            }

            // DEALLOCATIONS
            while (true)
            {
                for (std::size_t j = 0; j < current_phase_alloc_count; j++)
                {
                    std::size_t current_dealloction_attempt_index = (TOTAL_ALLOCATION_COUNT_PER_PHASE * phase) + j;

                    if (allocation_buckets[deallocation_bucket_index][current_dealloction_attempt_index].allocated == true && allocation_buckets[deallocation_bucket_index][current_dealloction_attempt_index].deallocated == false)
                    {
                        free(allocation_buckets[deallocation_bucket_index][current_dealloction_attempt_index].ptr);
                        allocation_buckets[deallocation_bucket_index][current_dealloction_attempt_index].deallocated = true; // To avoid double-frees
                        current_phase_dealloc_count++;
                    }
                }

                if (current_phase_dealloc_count == current_phase_alloc_count)
                {
                    break;
                }
            }
        }
    };

    std::array<std::unique_ptr<std::thread>, MAX_THREAD_COUNT> threads;

    for (auto i{ 0 }; i < thread_count; i++)
    {
        threads[i] = std::unique_ptr<std::thread>(new std::thread(thread_function, i, thread_count-1-i));
    }

    for(std::size_t i =0; i<thread_count; i++)
    {
        threads[i]->join();
    }

    std::size_t total_num_of_allocs_and_frees_in_all_threads = 0;
    
    std::size_t counter = 0;
    for(auto& bucket : allocation_buckets)
    {
        if(counter >= thread_count)
        {
            break;
        }

        for(auto& allocation : bucket)
        {
            if (allocation.ptr)
            {
                total_num_of_allocs_and_frees_in_all_threads++;

                if (allocation.allocated == false || allocation.deallocated == false) 
                { 
                    Console::print_colour(ConsoleColour::FG_RED, "TEST FAILED !!!\n"); 
                }
            }
        }

        counter++;
    }
    //////////////////////////////////////////////////////////////////
    stopwatch.stop();
    auto clock_cycles = stopwatch.get_elapsed_cycles();

    Console::print_colour(ConsoleColour::FG_BLUE, "Number of threads : " + std::to_string(thread_count) + "\n");
    Console::print_colour(ConsoleColour::FG_BLUE, "Number of allocs and frees per thread : " + std::to_string(total_num_of_allocs_and_frees_in_all_threads /thread_count) + "\n");
    Console::print_colour(ConsoleColour::FG_BLUE, "Interleave count : " + std::to_string(INTERLEAVE_COUNT) + "\n");
    Console::print_colour(ConsoleColour::FG_RED, "Clock cycles : " + std::to_string(clock_cycles) + "\n");

    std::ofstream outfile(samples_output_file, std::ios_base::app);
    outfile << clock_cycles << std::endl;
    outfile.close();
}

bool prepare_benchmark_data(std::size_t thread_count)
{
    // COMPUTING 1 PHASES ALLOCATION SIZES
    for (std::size_t thread_index = 0; thread_index < thread_count; thread_index++)
    {
        std::size_t allocation_index = 0;

        for (std::size_t size_class_index = 0; size_class_index < SIZE_CLASS_COUNT; size_class_index++)
        {
            std::size_t allocation_size = SIZE_CLASSES[size_class_index];
            std::size_t allocation_count = ALLOCATION_COUNTS_FOR_SIZE_CLASSES_PER_PHASE[size_class_index];

            for (std::size_t l = 0; l < allocation_count; l++)
            {
                allocation_buckets[thread_index][allocation_index].size_class = allocation_size;
                allocation_index++;
            }
        }
    }

    // COPYING INITIAL PHASE ALLOC SIZES TO THE REST OF PHASES ( INTERLEAVES )
    for (std::size_t thread_index = 0; thread_index < thread_count; thread_index++)
    {
        for (std::size_t phase = 1; phase < INTERLEAVE_COUNT; phase++)
        {
            for (std::size_t allocation_index = 0; allocation_index < TOTAL_ALLOCATION_COUNT_PER_PHASE; allocation_index++)
            {
                allocation_buckets[thread_index][(phase* TOTAL_ALLOCATION_COUNT_PER_PHASE) + allocation_index].size_class = allocation_buckets[thread_index][allocation_index].size_class ;
            }
        }
    }

    // NOW SHUFFLE
    if (SHUFFLE)
    {
        std::random_device rd;
        std::mt19937 g(rd());

        for (std::size_t i = 0; i < thread_count; i++)
        {
            constexpr std::size_t shuffle_count = TOTAL_ALLOCATION_COUNT_PER_PHASE * INTERLEAVE_COUNT;
            std::shuffle(allocation_buckets[i].begin(), allocation_buckets[i].begin() + shuffle_count, g);
        }
    }

    // VERIFY COMPUTED DATA
    auto allocation_event_per_thread = TOTAL_ALLOCATION_COUNT_PER_PHASE * INTERLEAVE_COUNT;
    for (std::size_t thread_index = 0; thread_index < thread_count; thread_index++)
    {
        for (std::size_t allocation_event_index = 0; allocation_event_index < allocation_event_index; allocation_event_index++)
        {
            if (allocation_buckets[thread_index][allocation_event_index].size_class == 0)
            {
                return false;
            }
        }
    }

    return true;
}

bool do_reads_writes_on_buffer(void* buffer, std::size_t buffer_size)
{
    if(buffer == nullptr)
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

void run_benchmark (int argc, char* argv[])
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

    if (prepare_benchmark_data(thread_count) == false)
    {
        Console::print_colour(ConsoleColour::FG_RED, "TEST FAILED !!!\n");
        return;
    }

    run_multithreaded_benchmark(thread_count);

    #ifdef _WIN32
    std::system("pause");
    #endif

}

#endif