RUNNING THE BENCHMARKS ON LINUX

```bash
chmod +x build.sh
./build.sh
For llmalloc , run benchmark_llmalloc
For IntelOneTBB , run benchmark_intelonetbb
```

RUNNING THE BENCHMARKS ON WINDOWS

```bash
build_msvc.bat
For llmalloc , run benchmark_llmalloc.exe
For IntelOneTBB , run benchmark_intelonetbb.exe
```

BUILDING INTEL ONE TBB FROM ITS SOURCE ON LINUX

```bash
cd /tmp
git clone https://github.com/uxlfoundation/oneTBB.git
cd oneTBB
mkdir build && cd build
# Configure: customize CMAKE_INSTALL_PREFIX and disable TBB_TEST to avoid tests build
cmake -DCMAKE_INSTALL_PREFIX=/tmp/my_installed_onetbb -DTBB_TEST=OFF ..
cmake --build .
cmake --install .
# Your installed oneTBB is in /tmp/my_installed_onetbb => /tmp/my_installed_onetbb/lib/libtbbmalloc_proxy.so
```

GETTING INTEL TBB ON WINDOWS

- Download and install IntelOneTBB SDK for includes, libs and DLLs : https://github.com/uxlfoundation/oneTBB/releases