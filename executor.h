//
// IO executor.
//

#pragma once

#include <memory>
#include <thread>
#include <map>
#include <ctime>
#include <assert.h>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <unordered_set>

#include "limits.h"
#include "connection.h"
#include "murmur3_hash.h"

namespace memcache {

/*!
 * \brief A synchronized FIFO queue.
 * Currently the queue as no size limits.
 * @tparam Item type.
 */
template<typename T>
struct sync_queue {
  typedef std::deque<T> queue;

  explicit sync_queue() {}

  /*!
   * \brief Push an item. Not expected to block.
   * @param v
   */
  void push(T v) {
    std::unique_lock<std::mutex> lock(m_);
    q_.push_back(std::move(v));
    if (q_.size() == 1)
      con_.notify_one();
  }

  /*!
   * Pops an item from the queue. Blocks if empty.
   * @return
   */
  T pop() {
    std::unique_lock<std::mutex> lock(m_);
    while (q_.empty()) {
      con_.wait(lock);
    }
    return next();
  }

  bool size() {
    std::unique_lock<std::mutex> lock(m_);
    return q_.size();
  }

  bool empty() {
    std::unique_lock<std::mutex> lock(m_);
    return q_.empty();
  }

private:
  std::mutex m_;
  std::condition_variable con_;
  queue q_;

  T next() {
    assert(!q_.empty());
    T v(std::move(q_.front()));
    q_.pop_front();
    return v;
  }
};

/*!
 * \brief Executor task struct.
 */
struct task {

  /*!
   * \brief Task type.
   */
  enum type {
    NOOP = 0,
    /*!
     * New session.
     */
    NEW,
    /*!
     * Read.
     */
    READ,
    /*!
     * Close.
     */
    CLOSE,
    /*!
     * Shutdown.
     */
    SHUTDOWN,
  };

  explicit task(type t, connection *s)
      : type_(t), s_(s) {}

  explicit task(type t, connection *s, buffer b)
      : type_(t), s_(s), packet_(std::move(b)) {}

  /*!
   * \brief Move constructor.
   * @param t
   */
  task(task &&t)
      : type_(t.type_), s_(t.s_), packet_(std::move(t.packet_)) {}

  type type_ = NOOP;

  /*!
   * brief Session for which this task was created.
   */
  connection *s_ = nullptr;

  /*!
   * \brief Incoming packet (that may be a part of the whole packet).
   */
  buffer packet_;

private:
  task(const task &) = delete;
  task &operator=(const task &) = delete;
};

/*!
 * \brief Executor class.
 * Backed by a sync_queue and processing thread.
 * Processes tasks in FIFO order.
 */
struct executor {
  explicit executor() {
    processor_.reset(new std::thread(std::bind(&executor::process, this)));
  }
  ~executor();

  /*!
   * \brief backing queue.
   */
  sync_queue<task> q_;

  void add(task &&d) {
    q_.push(std::move(d));
  }

private:
  // Disable copy.
  executor(const executor &) = delete;
  executor &operator=(const executor &) = delete;

  typedef std::unordered_set<connection *> connections;
  connections active_connections_;

  std::unique_ptr<std::thread> processor_;

  /*!
   * \brief Process task based on its type.
   * @param t task
   * @return True if successfully processed. False otherwise (means connection should be closed.)
   */
  bool process_inl(const task &t);

  /*!
   * \brief Add a new connection.
   * @param s
   */
  void add_connection(connection *s);

  /*!
   * \brief Close a connection.s
   * @param s
   */
  void close_connection(connection *s);

  /*!
   * \brief Add and buffer read data to the connection info object.
   * @param s connection
   * @param b data.
   */
  void put_new_data(connection *s, buffer b);

  /*!
   * \brief Cleanup all state.
   */
  void cleanup();

  /*!
   * \brief Executor thread process loop.
   */
  void process() {
    while (true) {
      auto v = q_.pop();
      if (v.type_ != task::SHUTDOWN) {
        assert(v.s_);
      }
      if (!process_inl(v))
        break;
    }

    cleanup();
  }
};
/*!
 * \brief IO thread pool executor.
 * Picks an executor in round-robin fashion.
 */
class IOPoolExecutor {
public:
  explicit IOPoolExecutor(int size = 0) {
    init(size);
  }
  ~IOPoolExecutor() {}

  void init(int size) {
    for (int i = 0;i < size;++i) {
      executors_.push_back(std::move(std::unique_ptr<executor>(new executor())));
    }
  }

  /*!
   * \brief Add task to the specified executor.
   * Pick the next round-robin executor if index is -1.
   * @param t Task
   * @param index executor index.
   */
  void add(task&& t, int index = -1) {
    assert(t.s_);
    assert(t.type_ != task::NOOP);
    if (index == -1) {
      index = pick();
    }

    executors_[index]->add(std::move(t));
  }

  /*!
   * \brief Pick the next round-robin executor.
   * @return Picked executor index.
   */
  int pick() {
    int picked = next_ % executors_.size();
    ++next_;
    return picked;
  }

  std::vector<std::unique_ptr<executor>> executors_;
private:

  uint64_t next_ = 0;

  IOPoolExecutor(const IOPoolExecutor &) = delete;
  IOPoolExecutor &operator=(const IOPoolExecutor &) = delete;
};
}