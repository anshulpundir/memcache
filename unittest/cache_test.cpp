#include <thread>
#include <vector>

#include "./../cache.h"
#include "../protocol_binary.h"

using namespace memcache;

/*!
 * \brief Build set header and request.
 * @param key
 * @param value
 * @param cas
 * @return constructed request.
 */
static std::string build_set_request(const std::string& key, const std::string& value, uint64_t cas = 0) {
  protocol_binary_request_header h;
  memset(&h, 0, sizeof(h));

  h.request.magic = (uint8_t) PROTOCOL_BINARY_REQ;
  h.request.opcode = (uint8_t) PROTOCOL_BINARY_CMD_SET;
  h.request.datatype = (uint8_t) PROTOCOL_BINARY_RAW_BYTES;

  h.request.keylen = (uint16_t) key.length();
  h.request.extlen = (uint8_t) 8;
  h.request.bodylen = (uint32_t) (key.length() + value.length() + 8);
  h.request.cas = cas;

  std::string ret;
  ret.assign(reinterpret_cast<const char* >(&h), sizeof(h));
  ret.append(8, 0);
  ret.append(key);
  ret.append(value);
  return ret;
}

/*!
 * brief Test cache get and set with and without cas.
 * @param cas cas
 */
void test_basic(int id = 0, uint64_t cas = 0) {
  cache c;
  for (int i = 0;i < 10;++i) {
    std::string key("key_" + std::to_string(id) + '_' + std::to_string(i));
    std::string val("val_"  + std::to_string(id) + '_' + std::to_string(i));
    std::string pak = build_set_request(key, val, cas);
    c.set(cache::value(std::move(pak), *util::get_header(pak)));
  }

  assert(c.count() == 10);

  // Cas with cas + 1 should have no effect.
  if (cas) {
    for (int i = 0;i < 10;++i) {
      std::string key("key_" + std::to_string(id) + '_' + std::to_string(i));
      std::string val("val_" + std::to_string(id) + '_' + std::to_string(10 * i + 1));
      std::string pak = build_set_request(key, val, cas + 1);
      c.cas(cache::value(std::move(pak), *util::get_header(pak)), cas + 1);
    }
  }

  // Verify.
  for (int i = 0;i < 10;++i) {
    std::string key("key_" + std::to_string(id) + '_' + std::to_string(i));
    std::string val("val_" + std::to_string(id) + '_' + std::to_string(i));
    std::string pak = build_set_request(key, val);
    cache::value v(std::move(pak), *util::get_header(pak));
    std::shared_ptr<cache::value> ret = c.get(v.get_key());
    assert(ret);
    assert(memcmp(val.data(), ret->get_value(), val.length()) == 0);
  }

  // Cas set to original should modify values.
  if (cas) {
    for (int i = 0;i < 10;++i) {
      std::string key("key_" + std::to_string(id) + '_' + std::to_string(i));
      std::string val("val_" + std::to_string(id) + '_' + std::to_string(10 * i + 1));
      std::string pak = build_set_request(key, val, cas);
      c.cas(cache::value(std::move(pak), *util::get_header(pak)), cas);
    }

    // Verify.
    for (int i = 0;i < 10;++i) {
      std::string key("key_" + std::to_string(id) + '_' + std::to_string(i));
      std::string val("val_" + std::to_string(id) + '_' + std::to_string(10 * i + 1));
      std::string pak = build_set_request(key, val);
      cache::value v(std::move(pak), *util::get_header(pak));
      std::shared_ptr<cache::value> ret = c.get(v.get_key());
      assert(ret);
      assert(memcmp(val.data(), ret->get_value(), val.length()) == 0);

      // Test remove with wrong cas
      bool r = c.remove(v, cas + 1);
      assert(!r);
      ret = c.get(v.get_key());
      assert(ret);

      // Test remove with correct cas
      r = c.remove(v, cas);
      assert(r);
      ret = c.get(v.get_key());
      assert(!ret);
    }
  }
}

/*!
 * brief Test cache remove with and without cas.
 * @param cas cas
 */
void test_remove(uint64_t cas = 0) {
  cache c;
  for (int i = 0;i < 10;++i) {
    std::string key("key_" + std::to_string(i));
    std::string val("val_" + std::to_string(i));
    std::string pak = build_set_request(key, val, cas);
    c.set(cache::value(std::move(pak), *util::get_header(pak)));
  }

  assert(c.count() == 10);

  // Verify.
  for (int i = 0;i < 10;++i) {
    std::string key("key_" + std::to_string(i));
    std::string val("val_" + std::to_string(i));
    std::string pak = build_set_request(key, val);
    cache::value v(std::move(pak), *util::get_header(pak));
    std::shared_ptr<cache::value> ret = c.get(v.get_key());
    assert(ret);
    assert(memcmp(val.data(), ret->get_value(), val.length()) == 0);
  }

  // Verify.
    for (int i = 0;i < 10;++i) {
      std::string key("key_" + std::to_string(i));
      std::string val("val_" + std::to_string(10 * i + 1));
      std::string pak = build_set_request(key, val);
      cache::value v(std::move(pak), *util::get_header(pak));

      // Cas set to original should modify values.
      if (cas) {
        // Test remove with wrong cas
        bool r = c.remove(v, cas + 1);
        assert(!r);
        auto ret = c.get(v.get_key());
        assert(ret);

        // Test remove with correct cas
        r = c.remove(v, cas);
        assert(r);
        ret = c.get(v.get_key());
        assert(!ret);
      } else {
        // Test remove
        auto r = c.remove(v, 0);
        assert(r);
        auto ret = c.get(v.get_key());
        assert(!ret);
      }
    }
}

void all_tests(int id = 0) {
  test_basic(id);
  test_basic(id, 999);
  test_remove(id);
  test_remove(999);
}

static void set(cache& c, std::string key, std::string val) {
  std::string pak = build_set_request(key, val);
  c.set(cache::value(std::move(pak), *util::get_header(pak)));
}

static std::shared_ptr<cache::value> get(cache& c, std::string key) {
  auto pak = build_set_request(key, "");
  cache::value v(std::move(pak), *util::get_header(pak));
  return c.get(v.get_key());
}

/*!
 * \brief Test freeing based on LRU.
 */
void test_free() {
  cache c;

  std::string key("key_1");
  std::string val("val_1");
  std::string pak = build_set_request(key, val);
  size_t target_size = 5 * pak.length();

  c.rehash(target_size);

  // Now set value;
  c.set(cache::value(std::move(pak), *util::get_header(pak)));
  assert(c.count() == 1);

  // Set 10 values. Only last 5 values should be in the cache.
  {
    for (int i = 0;i < 10;++i) {
      std::string key("key_" + std::to_string(i));
      std::string val("val_" + std::to_string(i));
      set(c, key, val);
    }

    assert(c.count() == 5);

    for (int i = 5;i < 10;++i) {
      std::string key("key_" + std::to_string(i));
      std::string val("val_" + std::to_string(i));
      auto ret = get(c, key);
      assert(ret);
      assert(memcmp(val.data(), ret->get_value(), val.length()) == 0);
    }
  }
}

int main() {

  all_tests();

  std::vector<std::thread> ts;
  for (int i = 0; i < 10;++i) {
    auto t = std::thread(all_tests, i);
    ts.push_back(std::move(t));
  }

  for (int i = 0; i < 10;++i) {
    ts[i].join();
  }

  test_free();
}