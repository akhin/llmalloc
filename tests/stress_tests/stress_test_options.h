#ifndef _STRESS_TEST_OPTIONS_H_
#define _STRESS_TEST_OPTIONS_H_

#include <cstddef>
#include <limits>
#include <vector>
#include <llmalloc.h>

struct StressTestOptions
{
    std::size_t thread_count = 16;
    bool cross_thread_deallocations = true;

    std::size_t iterations = 1000;
    std::size_t op_interleave_period = 4; // Means will do n deallocs after each n allocs and so on

    std::size_t max_size_for_data_verifications = 4096;

    std::vector<std::size_t> size_classes =
    {
        // Size classes fully aligned to their bins
        16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144,

        // Size classes fully aligned to their bins +1
        17, 33, 65, 129, 257, 513, 1025, 2049, 4097, 8193, 16385, 32769, 65537, 131073, 262145,

        // Size classes fully aligned to their bins -1
        15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191, 16383, 32767, 65535, 131071, 262143,

        // Non-power-of-two sizes
        19, 37, 123, 543, 1009, 2043, 4093, 8197, 16381, 65539, 131075, 262147,
        999, 1999, 4099, 8197, 12345, 54321, 99999,

        // Sub-page and cross-page sizes
        4095, 4096, 4097, 524288, 524289, 524287,

        // Large sizes
        300000, 500000, 750000, 1250000, 2000000, 3500000, 5000000, 10000000, 25000000,

        // Edge cases
        0, 1, 2, 3, 8, /*4294967296 causes us to go out of memory and stall */

    };
    
    llmalloc::ScalableMallocOptions scalable_malloc_options;
};

#endif