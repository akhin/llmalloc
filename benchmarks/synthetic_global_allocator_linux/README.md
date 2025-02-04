RUNNING THE BENCHMARKS

```bash
chmod +x *.sh
./build.sh
For llmalloc , run run_with_llmalloc.sh
For mimalloc , run run_with_mimalloc.sh
For IntelOneTBB , run run_with_intelonetbb.sh
For GNU LibC , run run_with_gnulibc_malloc.sh
```

BINARY VERSIONS

	INTELONETBB       2022.0.0
	MIMALLOC          2.1.9 ( 2025 01 03 )
	llmalloc          1.0.0
    GNU LIBC          Your environment
    
BUILDING LLMALLOC SO FROM ITS SOURCE

```bash
cd into linux_ld_preload_so directory
chmod +x *.sh
./build.sh
```

BUILDING INTELONETBB SO FROM ITS SOURCE

```bash
# Do our experiments in /tmp
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
    
BUILDING MIMALLOC SO FROM ITS SOURCE

```bash
git clone https://github.com/microsoft/mimalloc.git
cd into mimalloc 
mkdir -p out/release
cd out/release
cmake ../..
make
```