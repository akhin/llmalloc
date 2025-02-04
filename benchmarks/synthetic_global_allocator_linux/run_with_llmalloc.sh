#!/bin/bash
LD_PRELOAD=./llmalloc.so.1.0.0 ./benchmark $1
