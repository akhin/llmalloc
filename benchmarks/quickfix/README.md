DESCRIPTION

Server code uses thread per connection model to have multiple threads during the benchmarks.
Server does not log and output to the console at all.

Clients executable creates 16 threads with each thread continuously sending new orders to the server.
Server responds to new orders with ack execution report.

BUILDING ON LINUX

1. Follow the readme of Quickfix to build it on your system : https://github.com/quickfix/quickfix

2. Create a deps directory in this folder. Copy libquickfix.so to deps 

3. Create deps/include directory and copy all Quickfix headers into it.

RUNNING THE BENCHMARK

1. Copy the shared objects from "synthetic_global_allocator_linux" to "server" directory.

2. Run the server with one of the following shs :

- run_with_gnu_libc.sh
- run_with_intelonetbb.sh
- run_with_mimalloc.sh
- run_with_llmalloc.sh

3. Run the clients executable with run_clients.sh

4. In every 250kth message, the server will output percentiles of elapsed clock cycles.

5. When you repeat the benchmark for another allocator, you have to delete "store" folders under both clients and server directories to reset the sequence numbers.