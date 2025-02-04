RUNNING THE BENCHMARKS

        Run build_msvc.bat
        For MS UCRT , run benchmark_ucrt.exe
        For mimalloc , run benchmark_mimalloc.exe
        For IntelOneTBB , run benchmark_inteltbb.exe
        For llmalloc , run benchmark_llmalloc.exe

BINARY VERSIONS

        INTELONETBB         2022.0.0
        MIMALLOC            2.1.9 ( 2025 01 03 )
        LLMALLOC            1.0.0
        UCRT                Tied to your Windows

GETTING INTEL TBB ON WINDOWS

- Download and install IntelOneTBB SDK for includes, libs and DLLs : https://github.com/uxlfoundation/oneTBB/releases

BUILDING MIMALLOC 

- Get latest from Github : https://github.com/microsoft/mimalloc
- Go to IDE, build in release mode
- Collect include dir and collect mimalloc.lib from out/release