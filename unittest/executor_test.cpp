#include "./../executor.h"

using namespace memcache;

int main() {
  const int size = 8;
  memcache::IOPoolExecutor pool;
  pool.init(size);

  // test round robin.
  for (int i = 0;i < 8;++i) {
    assert(pool.pick() == i);
  }
}