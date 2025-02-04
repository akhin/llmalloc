ln -s ./libquickfix.so ../deps/libquickfix.so.16
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../deps
LD_PRELOAD=./libtbbmalloc.so.2 ./exchange_simulator
