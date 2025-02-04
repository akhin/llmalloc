#!/bin/bash
VERSION="1.0.0"
rm -f llmalloc*so* so_tests
# RELEASE VERSION
g++ -DNDEBUG -O3 -fno-rtti -shared -I../ -std=c++17  -fPIC -o llmalloc.so.${VERSION} llmalloc.cpp -lstdc++ -pthread -ldl
# RELEASE VERSION WITH -DUSE_ALLOC_HEADERS
g++ -DUSE_ALLOC_HEADERS -DNDEBUG -O3 -fno-rtti -shared -I../ -std=c++17  -fPIC -o llmalloc_use_alloc_headers.so.${VERSION} llmalloc.cpp -lstdc++ -pthread -ldl
# RELEASE WITH PERF TRACES
g++ -DENABLE_PERF_TRACES -DDISPLAY_ENV_VARS -DNDEBUG -O3 -fno-rtti -shared -I../ -std=c++17 -fPIC -o llmalloc_perf_traces.so.${VERSION} llmalloc.cpp -lstdc++ -pthread -ldl
# DEBUG VERSION
g++ -DDISPLAY_ENV_VARS -shared -I../ -std=c++17 -fPIC -g -fno-omit-frame-pointer -o llmalloc_debug.so.${VERSION} llmalloc.cpp -lstdc++ -pthread -ldl
# SO TESTS
g++ -DNDEBUG -O3 -fno-rtti -std=c++17  -o so_tests so_tests.cpp -lstdc++ -pthread