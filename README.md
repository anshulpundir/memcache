# memcache

A memcache implementation in modern C++. Support for get(), set(), and delete() and CAS (for set and delete) is provided using the the [binary protocol](https://cloud.github.com/downloads/memcached/memcached/protocol-binary.txt).

## build
### prerequisites
C++11 on linux. 
#### instructions
```sh
cd ./memcache && mkdir -p build && cd build && cmake .. && make -j
```

## testing
### unit-tests
```sh
./memcache && mkdir -p build && cd build && cmake .. && make -j && make test
```

### e2e tests
#### prerequisites
* pip
* [python-binary-memcached](https://github.com/jaysonsantos/python-binary-memcached) a python client implementation for the binary protocol
* pytest, mock, pytest-cov and flake8
* virtualenv and pip are good to have, but can be skipped. Install using instructions [here](http://www.saltycrane.com/blog/2010/02/how-install-pip-ubuntu/).

#### instructions
```sh
sudo apt-get install python-pip python-dev build-essential
sudo pip install --upgrade pip
sudo pip install python-binary-memcached
sudo pip install pytest
sudo pip install pytest-cov
sudo pip install mock
sudo pip install flake8
```
#### running
```sh
cd memcache/e2e_tests/ && sudo pytest -v -s test_set_get.py
```

## usage options
```sh
memcache -i ip -p port -t num_threads -m memory_in_mb
```

## high-level design/flow
We use epoll for I/O event notification. The main thread sets up the socket and creates the epoll instance and waits for incoming connections and/or data on those connections. 
The main thread examines the epoll events and creates a connection object/connection and assigns to it an executor from the IOPoolExecutor. 
The connection object is used to store state for the connection e.g. buffer previous packets, assigned IO executor etc. 

Work is passed off to the IO executor inside a task object, which includes the connection object inside it. The task executes connection functions and does things like buffering the incoming packet (until the entire packed it received) before calling into the global cache to perform get(), set() or delete().
The connection object also writes back directly to the socket to respond back to a request on a connection.

In the standard settings, IOPoolExecutor will have threads equal to the number of cores. 
Currently there is a single global cache object, access to which is locked using a mutex. The main lookup data structure inside the cache is an std::unordered_map. Eviction is done using LRU, using a std::list.

## performance
* Listening and handling of epoll events happens on the main thread. This is probably not terribly bad for performance since this is not CPU intensive work, however handling connections on the IO thread directly would work better.
* IO executors: Work is passed off from the main thread to the IO thread pool executor for validations and cache operations. So, validations on the data, writing back response etc happens in parallel.
* Cache instances: Cache operations are locked and this would give rise to some contention, since this involves some CPU. Having a separate cache/executor would definitely work better. An approach here could be to assign the executor based on the contents of the key (having a separate executor for this might be better).
* Zero copy: Move semantics are heavily used. However, some copying happens when moving data from the main thread to the executors. Using a zero-copy buffer, e.g. folly::IOBuf would be quite helpful to eliminate copying all together.
* Cache reclamation: currently runs in the set request, and kicks in when we run out of space. A better approach would be to proactively clean up in the background using low/high thresholds.

## design trade-offs
* Single cache instance: For simplicity, there is a single cache instance. Since this a data parallel application, better performance can be achieved by having a cache/executor to avoid contention all together.
* Separate thread pools for CPU vs IO work: The cpu threads can do cache operations without needing to block (reduce context switching) and take work from and pass back to the IO pool. Threads can also be pinned to cores.
* Thread/core: Having a thread/core is probably optimal for CPU intensive, non-blocking work. For IO work which blocks, more threads might be useful.

## alternate approaches considered
* Started by wangle library for networking, which is nicely setup to provide the networking infra and different executors (IO and CPU). Ran into some issues and there is hardly any documentation. This is for simplicity. A better approach would be to proactively reclaim entries in the background.

## resource management
* limits.h defines some basic limits for resource usage management. 
* Cache reclaim: When a set might take the cache memory consumption above the limit, we reclaim some entries according to LRU (currently 5x the size of the new entry).
* Memory limit does not include the usage for the STL stuff.

## productionisation
Apart from building an optimized binary, we would need to add support for efficient logging (with line-numbers and file names). We would also need to collect stats for the different operations (both for individual components and e2e).

## TODO
* Zero-copy.
* CPU executor and separate cache instance/executor.
* Better cache reclamation.
* Stats and profiling.
* Logging.
