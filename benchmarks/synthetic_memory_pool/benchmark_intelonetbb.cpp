#define TBB_PREVIEW_MEMORY_POOL 1

#include "benchmark.h"

#include <tbb/memory_pool.h>

#ifdef _WIN32
#pragma comment(lib,"tbb12.lib")
#pragma comment(lib,"tbbmalloc.lib")
#endif

template <typename T>
class IntelOneTBBScalablePool
{
    public:

        void* allocate()
        {
            return m_pool.malloc(sizeof(T));
        }
    
        void deallocate(void*ptr)
        {
            m_pool.free(ptr);
        }
        
    private:
        tbb::memory_pool<std::allocator<T>> m_pool;
};

int main (int argc, char* argv[])
{
    IntelOneTBBScalablePool<Foo> allocator;
    run_multithreaded_benchmark<IntelOneTBBScalablePool<Foo>>(argc, argv, "intel one tbb", allocator);
    return 0;
}