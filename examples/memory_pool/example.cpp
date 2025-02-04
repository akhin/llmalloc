#include <llmalloc.h>

#include <iostream>

using namespace std;

int main()
{
    llmalloc::ScalablePool<int> pool;

    if (pool.create() == false)
    {
        std::cout << "Pool creation failed\n";
        return -1;
    }

    void* ptr = nullptr;
    ptr = pool.allocate();
    pool.deallocate(ptr);

    return 0;
}