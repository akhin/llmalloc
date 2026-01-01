#include "../unit_test.h" // Always should be the 1st one as it defines UNIT_TEST macro

#include <llmalloc.h>
using namespace llmalloc;

#include <vector>
#include <memory>
#include <thread>
#include <cstring>
#include <iostream>
using namespace std;

using LocalHeapType = HeapPow2<>;
using CentralHeapType = HeapPow2<MPMCBoundedQueue<uint64_t, typename Arena::MetadataAllocator>, LockPolicy::USERSPACE_LOCK>;

using AllocatorType =
ScalableAllocator<
        CentralHeapType,
        LocalHeapType
>;

UnitTest unit_test;

int main(int argc, char* argv[])
{
    constexpr std::size_t ARENA_CAPACITY = 1024*1024*128;

    CentralHeapType::HeapCreationParams params_central;
    LocalHeapType::HeapCreationParams params_local;

    AllocatorType::get_instance().set_thread_local_heap_cache_count(1);
    AllocatorType::get_instance().set_enable_fast_shutdown(false);

    ArenaOptions arena_options;
    arena_options.cache_capacity = ARENA_CAPACITY;

    bool success = AllocatorType::get_instance().create(params_central, params_local, arena_options);
    if (!success) { std::cout << "Creation failed !!!\n"; }

    auto thread_function = [&](unsigned int cpu_id)
    {
        auto ptr = AllocatorType::get_instance().allocate(5);
        LLMALLOC_UNUSED(ptr);
    };

    auto central_heap = AllocatorType::get_instance().get_central_heap();

    unit_test.test_equals(central_heap->get_bin_logical_page_count(11), 32, "thread exit handling", "logical page count before transfer");

    std::vector<std::unique_ptr<std::thread>> threads;
    threads.emplace_back(new std::thread(thread_function, 0));

    for (auto& thread : threads)
    {
        thread->join();
    }

    unit_test.test_equals(central_heap->get_bin_logical_page_count(11), 64, "thread exit handling", "logical page count after transfer");

    ////////////////////////////////////// PRINT THE REPORT
    std::cout << unit_test.get_summary_report("ThreadExitHandling");
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