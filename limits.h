//
// Limits.
//

#include <assert.h>
#include <vector>
#include <unistd.h>

#pragma once

namespace memcache {
const size_t KB = 1024;
const size_t MB = KB * KB;

const int DEFAULT_NUM_THREADS = 8;

static const size_t MAX_KEY_SIZE = 250;
static const size_t MAX_VALUE_SIZE = MB;
static const size_t MAX_EPOLL_EVENTS = 128;
static const size_t MAX_CONNECTIONS = 512;
static const size_t DEFAULT_CACHE_CAPACITY = 64 * MB;

static const size_t PACKET_EXTRAS_SIZE = 8;

static const size_t DATA_READ_CHUNK_SIZE = 128;
}