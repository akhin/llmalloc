1. Getting and building redis with MALLOC=libc ( as its default is jemalloc ) :

```bash
git clone https://github.com/redis/redis.git
cd redis
make USE_JEMALLOC=no USE_TCMALLOC=no USE_TCMALLOC_MINIMAL=no
make install
```

2. cd into src directory and verify that redis is built with libc. The output should have malloc=libc, not jemalloc :

```bash
cd src
./redis-server --version
Redis server v=255.255.255 sha=dcd0b3d0:1 malloc=libc bits=64 build=bffb2d07bd808a8c
```

3. Copy all shared object files from benchmarks/global_allocator_linux and linux_ld_prelopad_so to redis/src. And cd into redis/src .

4. To run redis server, choose one of the below options 

- with GNU libc : 

```bash
./redis-server
```

- with IntelOneTBB : 

```bash
LD_PRELOAD=./libtbbmalloc.so.2 ./redis-server
```

- with mimalloc : 

```bash
LD_PRELOAD=./libmimalloc.so.1.9 ./redis-server
```

- with llmalloc : 

```bash
LD_PRELOAD=./llmalloc.so.1.0.0 ./redis-server
```

5. Then in another terminal, navigate to redis/src and run redis bm utility : 

```bash
./redis-benchmark --threads <thread_no>
```