#!/bin/bash
LD_PRELOAD=./libmimalloc.so.1.9 ./benchmark $1
