#include <iostream>

#include "executor.h"

namespace memcache {

executor::~executor() {
  add(task(task::SHUTDOWN, nullptr));
  if (processor_.get())
    processor_->join();
}

void executor::add_connection(connection *s) {
  assert(active_connections_.find(s) == active_connections_.end());
  active_connections_.insert(s);
}

void executor::close_connection(connection *s) {
  auto it = active_connections_.find(s);
  assert(it != active_connections_.end());

  delete *it;
  active_connections_.erase(it);
}

void executor::put_new_data(connection *s, buffer b) {
  assert(active_connections_.find(s) != active_connections_.end());

  if (!s->buffer_packet(std::move(b))) {
    close_connection(s);
  }
}

bool executor::process_inl(const task &t) {
  switch (t.type_) {
    case task::NEW:
      assert(t.packet_.empty());
      add_connection(t.s_);
      break;
    case task::READ:
      put_new_data(t.s_, std::move(t.packet_));
      break;
    case task::CLOSE:
      assert(t.packet_.empty());
      close_connection(t.s_);
      break;
    case task::SHUTDOWN:
      // XXX TODO do graceful shutdown.
      return false;
    default:
      assert(false);
      break;
  }

  return true;
}

void executor::cleanup() {
  for (auto &v: active_connections_) {
    delete v;
  }

  active_connections_.clear();
}
}