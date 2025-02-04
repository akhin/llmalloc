## <a name="intro"></a>**Custom integration example Doom3**  

Getting and building Doom3

1. Get the repo : git clone --recursive https://github.com/RobertBeckebans/RBDOOM-3-BFG.git
   
2. Buy Doom3 from Steam : https://store.steampowered.com/app/208200/DOOM_3/ And after installing it , you need to copy the assets to the "base" directory :
        
            from 
            
                C:\Program Files (x86)\Steam\steamapps\common\DOOM 3 BFG Edition\base

            to 
                base directory
   
3. Install Vulkan SDK https://www.lunarg.com/vulkan-sdk/ , and then edit neo/cmake-vs2022-win64-no-ffmpeg.bat file and add -DUSE_VULKAN=ON . ( This one is optional , it can be built with DX12 based on repo notes)
   
4. Run cmake-vs2022-win64-no-ffmpeg.bat ( cmake should be on system and available in PATH env variable )

5. Solution files will be under "build" directory , sibling to neo dir.

6. Open the Visual Studio solution and change all projects' C++ version to C++17.

7. Build and run.

To integrate llmalloc :

Note that by default https://github.com/RobertBeckebans/RBDOOM-3-BFG/blob/master/neo/idlib/Heap.cpp uses _aligned_malloc/posix_memalign for 16byte aligned allocations. The default allocate method of llmalloc already guarantees 16 byte alignments , therefore no need to use allocate_aligned.

1. Copy llmalloc.h to neo/idlib

2. In neo/idlib/Heap.cpp , include llmalloc.h and replace Mem_Alloc16 and MemFree16 with the ones below. ( Or just copy the Heap.cpp from this directory ) :

```cpp
#include "llmalloc.h"

void* Mem_Alloc16( const size_t size, const memTag_t tag )
{
    if (!size)
    {
        return nullptr;
    }

    static std::atomic<bool> llmalloc_initialised = false;

    if (llmalloc_initialised == false)
    {
        llmalloc::ScalableMalloc::get_instance().create();
        llmalloc_initialised = true;
    }

    return llmalloc::ScalableMalloc::get_instance().allocate(size);
}

void Mem_Free16( void* ptr )
{
    llmalloc::ScalableMalloc::get_instance().deallocate(ptr);
}
```