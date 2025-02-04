#define ENABLE_PERF_TRACES
 
#include <llmalloc.h>

#include "benchmark.h"

int main (int argc, char* argv[])
{
    llmalloc::ScalablePool<Foo> allocator;
    
    llmalloc::ScalablePoolOptions options;

    // ARENA
    options.arena_initial_size = 1024*1024*40;
    
    // THREAD LOCAL POOLS
    options.local_pool_initial_size = 655360;
    
    // CENTRAL POOL
    options.central_pool_initial_size = 65536;

    //options.use_huge_pages = true;

    if (allocator.create(options) == false)
    {
        std::cout << "pool creation failed\n";
        return -1;
    }

    run_multithreaded_benchmark<llmalloc::ScalablePool<Foo>>(argc, argv, "llmalloc scalable memory pool", allocator);
    return 0;
}