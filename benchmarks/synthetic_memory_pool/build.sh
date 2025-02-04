#!/bin/bash
rm -f benchmark_intelonetbb benchmark_llmalloc
g++ -I./ -I../ -I../../ -DNDEBUG  -O3 -fno-rtti -std=c++17 -o benchmark_llmalloc benchmark_llmalloc.cpp -pthread

if [ -f /etc/os-release ]; then
    . /etc/os-release
    if [[ "$ID" == "rhel" || "$ID_LIKE" == *"rhel"* ]]; then
        g++ -I./intelonetbb/include/ -I./ -I../ -DNDEBUG -O3 -fno-rtti -std=c++17 -o benchmark_intelonetbb benchmark_intelonetbb.cpp -pthread -ltbbmalloc -L./intelonetbb/lib/rhel9.4/ -D TBB_PREVIEW_MEMORY_POOL=1
    else
        g++ -I/usr/include/oneapi -I./ -I../ -DNDEBUG -O3 -fno-rtti -std=c++17 -o benchmark_intelonetbb benchmark_intelonetbb.cpp -pthread -ltbbmalloc -L/usr/lib/oneapi/lib -D TBB_PREVIEW_MEMORY_POOL=1
    fi
fi