#!/bin/bash
echo "Testing the default so..."
LD_PRELOAD=./llmalloc.so.1.0.0 ./so_tests
echo "Testing the USE_ALLOC_HEADERS so..."
LD_PRELOAD=./llmalloc_use_alloc_headers.so.1.0.0 ./so_tests
