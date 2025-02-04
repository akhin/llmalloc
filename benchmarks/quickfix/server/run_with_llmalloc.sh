ln -s ./libquickfix.so ../deps/libquickfix.so.16
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../deps
LD_PRELOAD=./llmalloc.so.1.0.0 ./exchange_simulator
