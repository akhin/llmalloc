/*
    ! IMPORTANT NOTE : USING HUGE PAGES WILL MAKE INTERNAL LOGICAL PAGE SIZE 2MB.
      ( BY DEFAULT 64KB FOR SMALL OBJECTS AND 512 KB FOR MEDIUM OBJECTS )
      THEREFORE YOU WILL BE OBSERVING MUCH HIGHER VM CONSUMPTION.

    - For ScalableMalloc shared object, you can also set it via env variable : llmalloc_use_huge_pages=1

    - To work with 2MB huge pages on Linux and  2MB or 1 GB huge pages on Windows , you may need to configure your system :

        Linux : /proc/meminfo should have non-zero "Hugepagesize" & "HugePages_Total/HugePages_Free" attributes
                  ( If HugePages_Total or HugePages_Free  is 0
                  then run "echo 20 | sudo tee /proc/sys/vm/nr_hugepages" ( Allocates 20 x 2MB huge pages )
                  Reference : https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt )

                  ( If THP is enabled , we will use madvise. Otherwise we will use HUGE_TLB flag for mmap.
                  To check if THP enabled : cat /sys/kernel/mm/transparent_hugepage/enabled
                  To disable THP :  echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
                  )

        Windows : SeLockMemoryPrivilege is required.
                    It can be acquired using gpedit.msc :
                    Local Computer Policy -> Computer Configuration -> Windows Settings -> Security Settings -> Local Policies -> User Rights Managements -> Lock pages in memory
*/
#include <llmalloc.h>
using namespace llmalloc;

#include <iostream>
using namespace std;

#include <vector>
#include <cstddef>

int main()
{
    // HUGE PAGE CHECK
    if (VirtualMemory::is_huge_page_available() == false)
    {
        #ifdef __linux__
        std::cout << "Huge page not available. Try to run \"echo 20 | sudo tee /proc/sys/vm/nr_hugepages\" ( Allocates 20 x 2mb huge pages ) \n";
        #else
        std::cout << "Huge page not available. You need to enable it using gpedit.msc\n";
        #endif
        return -1;
    }
    
    std::cout << "huge page size = " << VirtualMemory::get_minimum_huge_page_size() << " bytes" << std::endl;

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    // GLOBAL ALLOCATOR EXAMPLE , LOGICAL PAGE SIZES WILL BE 2 MB THEREFORE HEAP CREATIONS MAY TAKE A WHILE
    {
        ScalableMallocOptions options;
        options.use_huge_pages = true;
        
        if(ScalableMalloc::get_instance().create(options) == false)
        {
            std::cout << "Creation failed\n";
            return -1;
        }
        
        void* ptr = nullptr;
        ptr = ScalableMalloc::get_instance().allocate(42);
        ScalableMalloc::get_instance().deallocate(ptr);
    }
    
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    // POOL EXAMPLE
    {
        ScalablePool<int> pool;
        
        ScalablePoolOptions options;
        options.use_huge_pages = true;

        if (pool.create(options) == false)
        {
            std::cout << "Pool creation failed\n";
            return -1;
        }

        void* ptr = nullptr;
        ptr = pool.allocate();
        pool.deallocate(ptr);
    }
    
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////
    // SINGLE THREADED STL ALLOCATOR EXAMPLE
    {
        SingleThreadedAllocatorOptions options;
        options.use_huge_pages = true;
        
        if (SingleThreadedAllocator::get_instance().create(options) == false)
        {
            std::cout << "Single threaded allocator creation failed\n";
            return -1;
        }
        
        std::vector<std::size_t, STLAllocator<std::size_t>> v;

        for (std::size_t i = 0; i < 4096; i++)
        {
            v.push_back(i);
        }
    }

    return 0;
}