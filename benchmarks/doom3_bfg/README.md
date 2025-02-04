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

7. Copy Heap.cpp in this directory and llmalloc.h to neo/idlib

8. Copy "mimalloc" folder from benchmarks\global_allocator_windows to neo/idlib 

9. For all projects in the solution : add mimalloc dir to "Additional directories" and add "mimalloc/include" to include directories for release mode only


How to switch between llmalloc, mimalloc and default MSVC _aligned_malloc :

- To build with llmalloc , uncomment "#define USE_LLMALLOC" line and comment "#define USE_MIMALLOC"
- To build with mimalloc , uncomment "#define USE_MIMALLOC" and comment "#define USE_LLMALLOC"
- To build with MSVC allocator, comment both "#define USE_LLMALLOC" and #define USE_MIMALLOC" lines

Running the benchmark

1. Create a "tmp" directory in your C drive. By default samples will be written here. ( samples_msvc.txt & samples_llmalloc.txt & samples_mimalloc.txt )

2. Build the game in release mode. And when starting , choose "Start without debugging" in your VisualStudio,

3. In the game options disable vsync so the game will not try to fix the FPS.

4. The elapsed clock cycles in every 100000th allocation now will be written to those files.

5. Choose a level and simply play the game. Make sure you delete the sample files under c:\tmp before after the level loading.