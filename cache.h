#pragma once

#include <assert.h>
#include <unordered_map>
#include <vector>
#include <list>
#include <string.h>
#include <functional>
#include <mutex>
#include <memory>
#include <iostream>

#include "protocol_binary.h"
#include "limits.h"
#include "util.h"
#include "murmur3_hash.h"

namespace memcache {
/*!
 * \brief LRU cache. Lookups using std::unordered_map and LRU using
 * std::list.
 *
 * All external operations a locked using a std::mutex.
 * We reclaim entries when we run out of pre-set memory capacity.
 */
  struct cache {

    /*!
     * \brief Cache key.
     */
		struct key {
      explicit key(const char* d, size_t len,
                   size_t memsize = 0) :
          key_ptr_(d), length_(len), value_size_(memsize) {}

			const char* key_ptr_ = nullptr;
			size_t length_ = 0;
			size_t value_size_ = 0;

			bool operator==(const key& k) const {
				if (this == &k) {
					return true;
        }

				if (length_ != k.length_) {
					return false;
				}

				return length_ == 0 || ::memcmp(key_ptr_, k.key_ptr_, length_) == 0;
			}
		};

    /*!
     * \brief Cache value.
     * Stores the entire write packet and also the LRU reference.
     */
		struct value {
      explicit value(std::string&& d, const protocol_binary_request_header& h)
          :data_str_(std::move(d)), header_(h) {
        assert(data_str_.size() >= header_.request.extlen + sizeof(header_));
      }

      value(value&& v) :data_str_(std::move(v.data_str_)), header_(v.header_),
                      lru_ref_(v.lru_ref_) {}

      std::string data_str_;
			protocol_binary_request_header header_;

      typedef std::list<key>::iterator lru_ref;
      lru_ref lru_ref_;

			key get_key() const {
				return key(data_str_.data() + sizeof(header_) + header_.request.extlen
						,header_.request.keylen
						,data_str_.size()
						);
			}

      protocol_binary_request_header* header() const {
        return (protocol_binary_request_header* ) data_str_.data();
      }

      const size_t packet_data_len() const {
        return data_str_.size() - header_.request.extlen - sizeof(header_);
      }

			const char* packet_user_data() const {
        return data_str_.data() + header_.request.extlen + sizeof(header_);
			}

			const size_t packet_value_len() const {
				return packet_data_len() - header_.request.keylen;
			}

      const char* get_value() {
        return packet_user_data() + header_.request.keylen;
      }

			void set_lru(lru_ref it) {
				lru_ref_ = it;
			}

		private:
			value(const value&) = delete;
			value& operator=(const value&) = delete;
		};

    cache(size_t capacity = 0) : capacity_(capacity) {
      if (capacity_ == 0) {
        capacity_ = DEFAULT_CACHE_CAPACITY;
      }

      assert(capacity_);

      size_t unit_mem =  sizeof(protocol_binary_request_header) + (MAX_VALUE_SIZE + MAX_KEY_SIZE);

      // Twice the number of max sized values.
      // XXX TODO Make this pluggable.
      size_t items = 2 * (capacity_/unit_mem);

      // XXX TODO set according to optimal load factor.
      lookup_.reserve(items);
    }

		~cache() {}

    /*!
     * \brief Reset capacity. Used for testing.
     * @param capacity
     */
    void rehash(size_t capacity) {
      clear();

      assert(capacity);
      capacity_ = capacity;
    }

		std::shared_ptr<value> get(const key& k) {
      std::unique_lock<std::mutex> lock(mutex_);
      return get_inl(k);
    }

		void set(value v) {
      std::unique_lock<std::mutex> lock(mutex_);
      set_inl(std::move(v));
    }

		bool cas(value v, uint64_t cas);
		bool remove(const value& v, uint64_t cas);

    size_t count() const {
      return lookup_.size();
    }

    void clear() {
      if (size_)
        reclaim(size_);
    }
	
	private:
    struct hasher {
      size_t operator()(const key& k) const {
        return MurmurHash3_x86_32(k.key_ptr_, k.length_);
      }
    };

    std::mutex mutex_;
		size_t capacity_ = 0;
		size_t size_ = 0;
    std::unordered_map<key, std::shared_ptr<value>, hasher> lookup_;
    std::list<key> lru_;

		cache(const cache&) = delete;
		cache& operator=(cache&) = delete;

    /*!
     * \brief Reclaim size worth of entires using LRU.
     * @param size
     */
    void reclaim(size_t size);

    std::shared_ptr<value> get_inl(const key& k) {
      auto it = lookup_.find(k);
      if (it == lookup_.end())
        return std::shared_ptr<value>();

      assert(!lru_.empty());

      // Reset LRU.
      if (it->second->lru_ref_ != --lru_.end()) {
        lru_.splice(lru_.end(), lru_, it->second->lru_ref_);
      }

      return it->second;
    }

    void set_inl(value v) {
      key k = v.get_key();

      auto it = lookup_.find(k);
      if (it != lookup_.end()) {
        bool ret = delete_inl(k);
        assert(ret);
      }

      size_t mem = v.data_str_.length();

      if (mem + size_ > capacity_) {
        // Free 5x the new item size.
        // XXX TODO Make this a pluggable policy.
        reclaim(5 * mem);
      }

      auto it2 = lru_.insert(lru_.end(), k);
      v.set_lru(it2);

      lookup_.emplace(std::make_pair(k, std::make_shared<value>(std::move(v))));
      size_ += mem;
    }

    bool delete_inl(key k) {
      auto it = lookup_.find(k);
      if (it == lookup_.end()) {
        return false;
      }

      //remove from LRU
      lru_.erase(it->second->lru_ref_);
      lookup_.erase(it);
      size_ -= k.value_size_;
      return true;
    }
	};

}