#!/bin/bash
echo "Testing with Python..."
LD_PRELOAD=./llmalloc.so.1.0.0 python3 test.py
LD_PRELOAD=./llmalloc_use_alloc_headers.so.1.0.0 python3 test.py
echo "Testing the default version with GDB : You will need to quit from GDB"
LD_PRELOAD=./llmalloc.so.1.0.0 gdb
echo "Testing the USE_ALLOC_HEADERS version with GDB : You will need to quit from GDB"
LD_PRELOAD=./llmalloc_use_alloc_headers.so.1.0.0 gdb