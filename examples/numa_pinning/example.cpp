/*
    - You need libnuma ( For ex : RHEL -> sudo yum install numactl-devel & Ubuntu -> sudo apt install libnuma-dev ) and -lnuma for GCC

    - For ScalableMalloc, you can also set it via env variable : llmalloc_numa_node
*/
#if (! defined(__linux__))
#error "NUMA pinning is supported only for Linux"
#endif

#define ENABLE_NUMA
#include <llmalloc.h>
using namespace llmalloc;

#include <numa.h>
#include <sched.h>

#include <iostream>
using namespace std;

#define TARGET_NUMA_NODE_TO_BIND 1
int get_numa_node_of_address(void* ptr);

int main()
{
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    // GLOBAL ALLOCATOR EXAMPLE
    {
        ScalableMallocOptions options;
        options.numa_node = TARGET_NUMA_NODE_TO_BIND;

        if(ScalableMalloc::get_instance().create(options) == false)
        {
            std::cout << "Creation failed\n";
            return -1;
        }

        void* ptr = nullptr;
        ptr = ScalableMalloc::get_instance().allocate(42);

        if(get_numa_node_of_address(ptr) != TARGET_NUMA_NODE_TO_BIND)
        {
            std::cout << "Address is not NUMA local NUMA pinning failed !\n";
            ScalableMalloc::get_instance().deallocate(ptr);
            return -1;
        }

        ScalableMalloc::get_instance().deallocate(ptr);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    // MEMORY POOL EXAMPLE
    {
        ScalablePool<int> pool;
        
        ScalablePoolOptions options;
        options.numa_node = TARGET_NUMA_NODE_TO_BIND;

        if (pool.create(options) == false)
        {
            std::cout << "Pool creation failed\n";
            return -1;
        }

        void* ptr = nullptr;
        ptr = pool.allocate();
        
        if(get_numa_node_of_address(ptr) != TARGET_NUMA_NODE_TO_BIND)
        {
            std::cout << "Address is not NUMA local NUMA pinning failed !\n";
            pool.deallocate(ptr);
            return -1;
        }

        pool.deallocate(ptr);
    }
    
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    // SINGLE THREADED STL ALLOCATOR EXAMPLE
    {
        SingleThreadedAllocatorOptions options;
        options.numa_node = TARGET_NUMA_NODE_TO_BIND;

        if (SingleThreadedAllocator::get_instance().create(options) == false)
        {
            std::cout << "Single threaded allocator creation failed\n";
            return -1;
        }

        void* ptr = nullptr;
        ptr = SingleThreadedAllocator::get_instance().allocate(42);

        if(get_numa_node_of_address(ptr) != TARGET_NUMA_NODE_TO_BIND)
        {
            std::cout << "Address is not NUMA local NUMA pinning failed !\n";
            SingleThreadedAllocator::get_instance().deallocate(ptr);
            return -1;
        }

        SingleThreadedAllocator::get_instance().deallocate(ptr);
    }

    return 0;
}

int get_numa_node_of_address(void* ptr)
{
    int actual_numa_node = -1;
    get_mempolicy(&actual_numa_node, nullptr, 0, (void*)ptr, MPOL_F_NODE | MPOL_F_ADDR);
    return actual_numa_node;
}