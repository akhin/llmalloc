ln -s ./libquickfix.so ../deps/libquickfix.so.16
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../deps
./exchange_simulator
