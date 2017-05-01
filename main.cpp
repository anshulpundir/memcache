#include <iostream>
#include <assert.h>
#include <sstream>
#include <unistd.h>
#include <signal.h>
#include <fstream>

#include "limits.h"
#include "network.h"
#include "executor.h"
#include "cache.h"
#include "limits.h"
#include "util.h"

/*!
 * Global IO thread pool executor.
 */
memcache::IOPoolExecutor io_pool;

/*!
 * Global cache.
 */
std::unique_ptr<memcache::cache> cache;

/*!
 * Check epoll event error.
 * @param e epoll_event
 * @return True if errored. False otherwise.
 */
static bool is_event_error(const epoll_event& e) {
  return ((e.events & EPOLLERR) || (e.events & EPOLLHUP)) ? true : false;
}

/*!
 *
 * @param s
 * @param ep
 * @param server_pool
 * @return
 */
static bool accept(memcache::socket &s, memcache::EpollHelper &ep,
                   memcache::IOPoolExecutor &server_pool) {
  memcache::connection_data info;
  while (s.connect(&info)) {
    // Pick an executor.
    int executor_index = io_pool.pick();

    // Create session and assign executor.
    memcache::connection* ses = new memcache::connection(info.fd_, *cache, executor_index);

    if (!ep.add_descriptor(info.fd_, ses)) {
      std::cerr << "Count not add descriptor!";
      delete ses;
      return false;
    }

    // Add task to IO executor.
    io_pool.add(std::move(memcache::task(memcache::task::NEW, ses)), executor_index);
  }

  return true;
}

int default_threads() {
  int threads = std::thread::hardware_concurrency();
  if (threads == 0)
    threads = memcache::DEFAULT_NUM_THREADS;

  return threads;
}

/*!
 * Listen for connections and push the incoming data chunks to mc::server for processing.
 * @param s
 * @param maxevents
 * @param threads
 * @param max_connections
 */
static void listen_loop(memcache::socket &s, unsigned int maxevents,
                        unsigned int threads, unsigned int max_connections) {
  assert(maxevents);
  assert(threads);

  // create server pool
  io_pool.init(threads);

  // Init epoll and listen.
  memcache::EpollHelper ep(maxevents);
  ep.open();
  ep.listen_socket(s);

  // Loop and process.
  while (true) {
    // Wait for events
    int n = ep.wait();
    if (n < 0) {
      std::cerr << "Error while calling wait.." << std::endl;
      return;
    }

    // Handle received events
    for (int i = 0; i < n; ++i) {
      epoll_event& e = ep.events_[i];

      // Handle errors.
      if (is_event_error(e)) {
        //
        if (&s != static_cast<memcache::socket* >(e.data.ptr)) {
          if (e.data.ptr) { //clean up the active session
            memcache::connection* conn = static_cast<memcache::connection* >(e.data.ptr);
            std::cerr << "Error for connection with fd: " << conn->fd_ << std::endl;

            // Close connection.
            io_pool.add(memcache::task(memcache::task::CLOSE, conn), conn->executor_index_);
          }
        } else {
          std::cerr << "Epoll event error for socket: " << s.fd() << std::endl;
        }
        continue;
      }

      // Handle new connections.
      if (&s == static_cast<memcache::socket* >(e.data.ptr)) {
        // Accept connection.
        bool acc = accept(s, ep, io_pool);
        if (!acc) {
          std::cerr << "Couldn't accept connection.";
          return;
        }
      } else {
        // Handle event for existing connection.
        memcache::connection* conn = static_cast<memcache::connection* >(e.data.ptr);
        assert(conn);

        // Read data in chunks
        while(true) {
          memcache::buffer buf;
          buf.resize(memcache::DATA_READ_CHUNK_SIZE);

          // Read data on the connection fd.
          ssize_t count = ::read(conn->fd_, &buf[0], buf.size());

          // Read error.
          if (count == -1) {
            if (errno != EAGAIN) {
              std::cerr << "read error: " << conn->fd_ << " err no:" << errno << std::endl;
              count = 0;
            } else {
              break;
            }
          }

          assert(count <= buf.size());

          // Successful read. Add to IO executor for processing.
          if (count > 0) {
            // Resize buf to have the correct size.
            buf.erase(buf.begin() + count, buf.end());

            io_pool.add(memcache::task(memcache::task::READ,
                                        conn, std::move(buf)), conn->executor_index_);
          } else {
            // We didn't read anything. Close the connection.
            assert(!count);
            io_pool.add(memcache::task(memcache::task::CLOSE, conn), conn->executor_index_);
            break;
          }
        }
      }
    }
  }
}

static void usage_help() {
  std::cerr << "memcache usage: " << std::endl
            << "  -i IP address of the listening socket. Defaults to 127.0.0.1" << std::endl
            << "  -p Port. Defaults to 11211" << std::endl
            << "  -t Processing threads (cache lookups). Defaults to number of cores and then to 8." << std::endl
            << "  -m Max cache memory in MB. Defaults to 64" << std::endl;
}

void set_logfile() {
  std::ofstream out("memcashew.log");
  std::clog.rdbuf(out.rdbuf());
}

int main(int argc, char* argv[]) {
  //set_logfile();

  // Parse args.
  memcache::options o;
  if (!memcache::util::parse(argc, argv, o)) {
    usage_help();
  }

  // Initialize threads.
  if (!o.threads) {
    o.threads = default_threads();
    assert(o.threads);
  }

  std::clog << "Listening on: " << o.ip << ":" << o.port
            << " threads:" << o.threads << " memory limit:" << o.cachemem
            << "MB" << " max connections:" << o.max_connections << std::endl;

  //allocate cache
  cache.reset(new memcache::cache(o.cachemem));

  // Setup a TCP socket and listen.
  memcache::socket s;
  if (s.bind(o.ip, o.port)) {
    std::clog << "socket created..." << std::endl;

    // run it
    listen_loop(s, memcache::MAX_EPOLL_EVENTS, o.threads, o.max_connections);
  } else {
    std::clog << "socket creation failed" << std::endl;
  }

  return 0;
}