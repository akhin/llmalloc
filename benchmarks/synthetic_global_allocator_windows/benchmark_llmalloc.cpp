#define ENABLE_PERF_TRACES
#define ENABLE_OVERRIDE
#define DISABLE_OVERRIDE_AUTO_INITIALISATIONS
#include "../../llmalloc.h"

#include <cstddef>
#include "benchmark.h"

int main (int argc, char* argv[])
{
    llmalloc::ScalableMallocOptions options;
    llmalloc::ScalableMalloc::get_instance().create(options);
    
    run_benchmark(argc, argv);
    return 0;
}