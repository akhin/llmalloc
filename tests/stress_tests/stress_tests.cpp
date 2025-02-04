/////////////////////////////////////////////////////////////////////////////////////////
//#define USE_ALLOC_HEADERS

//#define ENABLE_CONSOLE_PRINT
/////////////////////////////////////////////////////////////////////////////////////////
#include "stress_test_options.h"
#include "stress_test_runner.h"

#include <string>
#include <iostream>

int main(int argc, char* argv[])
{
    constexpr int max_mode_no = 3;
    int mode = 0;// default
    
    try
    {
        StressTestOptions options;
        if (argc > 1)
        {
            mode = std::stoi(argv[1]);

            if(mode > max_mode_no)
            {
                std::cout << "Invalid mode\n";
                return -1;
            }
        }
        else
        {
            std::cout << "Enter a mode number ( 0:default 1:stress on recycles 2:stress on grows 3: central heap only ) :\n";
            std::cin >> mode;
        }

        #ifdef USE_ALLOC_HEADERS
        std::cout << "Build type : USE_ALLOC_HEADERS\n";
        #else
        std::cout << "Build type : NO USE_ALLOC_HEADERS\n";
        #endif

        // MODE_DEFAULT
        if (mode == 0)
        {
            std::cout << "MODE_DEFAULT\n";
        }
        // MODE_STRESS_ON_RECYCLES
        else if (mode == 1)
        {
            std::cout << "MODE_STRESS_ON_RECYCLES\n";

            options.op_interleave_period = 1;
            options.iterations = 10000;

            options.size_classes.clear();
            for (std::size_t i = 0; i < 10; i++) options.size_classes.push_back(32);
            for (std::size_t i = 0; i < 10; i++) options.size_classes.push_back(120000);

            options.scalable_malloc_options.page_recycling_threshold = 0;

            for (std::size_t i = 0; i < llmalloc::HeapPow2<>::BIN_COUNT; i++)
            {
                options.scalable_malloc_options.local_logical_page_counts_per_size_class[i] = 1;
                options.scalable_malloc_options.central_logical_page_counts_per_size_class[i] = 1;
            }
        }
        // MODE_STRESS_ON_GROWS
        else if (mode == 2)
        {
            std::cout << "MODE_STRESS_ON_GROWS\n";
            
            //////////////////////////////////////////
            // OOM kills on Linux with higher values , even if we default grow_coefficient
            options.iterations = 100;
            options.thread_count = 8;
            //////////////////////////////////////////

            options.scalable_malloc_options.grow_coefficient = 0;

            options.scalable_malloc_options.page_recycling_threshold = 1;
            
            options.scalable_malloc_options.deallocation_queues_processing_threshold = 1;

            for (std::size_t i = 0; i < llmalloc::HeapPow2<>::BIN_COUNT; i++)
            {
                options.scalable_malloc_options.local_logical_page_counts_per_size_class[i] = 1;
                options.scalable_malloc_options.central_logical_page_counts_per_size_class[i] = 1;
            }
        }
        // MODE_CENTRAL_HEAP_ONLY
        else if ( mode == 3 )
        {
            std::cout << "MODE_CENTRAL_HEAP_ONLY\n";
            options.scalable_malloc_options.local_heaps_can_grow = false;
            options.scalable_malloc_options.page_recycling_threshold = 1;

            for (std::size_t i = 0; i < llmalloc::HeapPow2<>::BIN_COUNT; i++)
            {
                options.scalable_malloc_options.local_logical_page_counts_per_size_class[i] = 1;
            }
        }

        StressTestRunner runner;
        runner.run(options);
        runner.join();
    }
    catch (const std::runtime_error& ex)
    {
        std::cout << "Exception : " << ex.what() << "\n";
        return -1;
    }
    catch (...)
    {
        std::cout << "Unknown exception\n";
        return -1;
    }

    std::cout << "All good\n\n";

    return 0;
}