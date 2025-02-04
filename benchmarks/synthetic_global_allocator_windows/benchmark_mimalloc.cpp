#pragma comment(lib, "mimalloc.lib")
#include <mimalloc.h>
#include <mimalloc-override.h>
#include "benchmark.h"

int main (int argc, char* argv[])
{
    run_benchmark(argc, argv);
    return 0;
}