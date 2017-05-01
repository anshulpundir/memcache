#include "cache.h"

namespace memcache {

bool cache::remove(const value &v, uint64_t cas) {
  std::unique_lock<std::mutex> lock(mutex_);

  if (cas > 0) {
    auto p = get_inl(v.get_key());
    if (p && p->header_.request.cas != cas) {
      return false;
    }
  }

  return delete_inl(v.get_key());
}

bool cache::cas(value v, uint64_t cas) {
  std::unique_lock<std::mutex> lock(mutex_);

  if (cas > 0) {
    std::shared_ptr<value> p = get_inl(v.get_key());
    if (p && p->header_.request.cas != cas) {
      return false;
    }
  }

  set_inl(std::move(v));
  return true;
}

void cache::reclaim(size_t size) {
  assert(size);

  size_t freed = 0;
  //remove according to LRU
  for (auto it = lru_.begin(); it != lru_.end() && freed < size;) {
    freed += it->value_size_;
    auto tmp = it;
    ++it;
    bool d = delete_inl(*tmp);
    assert(d);
  }
}
}
