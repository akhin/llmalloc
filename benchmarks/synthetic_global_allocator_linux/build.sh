#!/bin/bash
rm -f benchmark
g++ -DNDEBUG -O3 -fno-rtti -I../../ -std=c++17 -o benchmark benchmark.cpp -pthread