//
// Common utility functions.
//

#pragma once

#include <arpa/inet.h>
#include <endian.h>
#include <byteswap.h>

#include "protocol_binary.h"

namespace {
unsigned long long htonll(unsigned long long val) {
  if (__BYTE_ORDER == __BIG_ENDIAN) {
    return (val);
  } else {
    return bswap_64(val);
  }
}

unsigned long long ntohll(unsigned long long val) {
  if (__BYTE_ORDER == __BIG_ENDIAN) {
    return (val);
  } else {
    return bswap_64(val);
  }
}
}

namespace memcache {

typedef std::vector<unsigned char> buffer;

struct options {
  unsigned int port = 11211;
  unsigned int threads = 0;
  unsigned int cachemem = DEFAULT_CACHE_CAPACITY;
  unsigned int max_connections = MAX_CONNECTIONS;
  std::string ip = "127.0.0.1";
};

class util {
public:
  static void print_header(const protocol_binary_request_header &h) {
    std::clog << "op=" << (unsigned int) h.request.opcode
              << " extlen=" << (unsigned int) h.request.extlen
              << " keylen=" << (unsigned int) h.request.keylen
              << " bodylen=" << (unsigned int) h.request.bodylen
              << " cas=" << (unsigned int) h.request.cas
              << std::endl;
  }

  static protocol_binary_request_header *get_header(const std::string &packet) {
    return (protocol_binary_request_header *) packet.data();
  }

  static std::string get_key(const std::string &packet) {
    std::string key;
    protocol_binary_request_header* header = get_header(packet);
    key.assign(packet.data() + sizeof(protocol_binary_request_header) + header->request.extlen,
               header->request.keylen);
  }

  static bool parse(int argc, char* argv[], options& o) {
    for (int i = 1; i < argc; ++i) {
      const char *arg = argv[i];
      if (!*arg)
        continue;

      if (arg[0] != '-' || !arg[1]) {
        return false;
      }

      if (arg[2]) {
        return false;
      }

      // Parse args.
      switch (arg[1]) {
        case 'i':
          if (i + 1 == argc) {
            return false;
          }
          // IP
          o.ip = std::string(argv[++i]);
          break;
        case 'p':
          if (i + 1 == argc) {
            return false;
          }
          // Port
          o.port = atoi(argv[++i]);
          break;
        case 't':
          if (i + 1 == argc) {
            return false;
          }
          // Threads for cache lookups.
          o.threads = atoi(argv[++i]);
          break;
        case 'm':
          if (i + 1 == argc) {
            return false;
          }
          // Memory
          o.cachemem = atoi(argv[++i]);
          if (!o.cachemem) {
            return false;
          }
          break;
        default:
          return false;
      }
    }

    return true;
  }

  static buffer build_response_hdr(const protocol_binary_request_header &h,
                                   unsigned short keylen, unsigned int body_len,
                                   unsigned int err = 0, unsigned char extlen = 0) {
    protocol_binary_response_header r;
    memset(&r, 0, sizeof(r));

    r.response.magic = (uint8_t) PROTOCOL_BINARY_RES;
    r.response.opcode = h.request.opcode;
    r.response.datatype = (uint8_t) PROTOCOL_BINARY_RAW_BYTES;
    r.response.opaque = h.request.opaque;
    r.response.cas = htonll(h.request.cas);

    r.response.keylen = (uint16_t) htons((uint16_t) keylen);
    r.response.extlen = (uint8_t) extlen;
    r.response.bodylen = htonl((uint32_t) body_len);

    r.response.status = (uint16_t) htons((uint16_t) err);
    return buffer((unsigned char *) &r, (unsigned char *) &r + sizeof(r));
  }

  /*!
   * \brief Validate the header according to protocol.
   * @return
   */
  static protocol_binary_response_status
    validate_header(const protocol_binary_request_header& header_) {
    if (header_.request.keylen == 0) {
      return PROTOCOL_BINARY_RESPONSE_E2BIG;
    }

    switch (header_.request.opcode) {
      case PROTOCOL_BINARY_CMD_GET:
        if (header_.request.extlen != 0 ||
            header_.request.bodylen != header_.request.keylen) {
          return PROTOCOL_BINARY_RESPONSE_EINVAL;
        }
        break;
      case PROTOCOL_BINARY_CMD_SET:
        if (header_.request.extlen != 8 ||
            header_.request.bodylen < header_.request.keylen + PACKET_EXTRAS_SIZE ||
            header_.request.keylen > MAX_KEY_SIZE) {
          return PROTOCOL_BINARY_RESPONSE_EINVAL;
        }

        if (header_.request.bodylen > MAX_VALUE_SIZE + header_.request.keylen + PACKET_EXTRAS_SIZE) {
          return PROTOCOL_BINARY_RESPONSE_E2BIG;
        }
        break;
      case PROTOCOL_BINARY_CMD_DELETE:
        if (header_.request.extlen != 0 ||
            header_.request.bodylen != header_.request.keylen) {
          return PROTOCOL_BINARY_RESPONSE_EINVAL;
        }
        break;
      default:
        return PROTOCOL_BINARY_RESPONSE_UNKNOWN_COMMAND;
        break;
    }

    return PROTOCOL_BINARY_RESPONSE_SUCCESS;
  }

  void log(std::string s) {
    std::cerr << __FILE__  << " " <<  __LINE__ << " " << s;
  }
};
}

