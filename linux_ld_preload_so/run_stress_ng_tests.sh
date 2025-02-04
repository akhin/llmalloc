#!/bin/bash
#
# stress-ng --help | grep malloc to see what can be done
#
NUMBER_OF_OPS=100000000
THREAD_COUNT=8

# Testing the default version with stress-ng
echo "Testing the default version with stress-ng, $THREAD_COUNT threads, $NUMBER_OF_OPS operations"
LD_PRELOAD=./llmalloc.so.1.0.0 stress-ng --malloc $THREAD_COUNT --malloc-ops $NUMBER_OF_OPS --verify --verbose

# Testing the USE_ALLOC_HEADERS version with stress-ng
echo "Testing the USE_ALLOC_HEADERS version with stress-ng, $THREAD_COUNT threads, $NUMBER_OF_OPS operations"
LD_PRELOAD=./llmalloc_use_alloc_headers.so.1.0.0 stress-ng --malloc $THREAD_COUNT --malloc-ops $NUMBER_OF_OPS --verify --verbose