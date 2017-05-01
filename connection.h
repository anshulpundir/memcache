//
// Connection state
//

#pragma once

#include <vector>
#include <stdint.h>
#include <unistd.h>

#include "protocol_binary.h"
#include "cache.h"

namespace memcache {
/*!
 * \brief Conenction state.
 * Stores the communicating socket (used to write responses),
 * the cache object for performing cache operations, and the
 * index of IO executor on which to process the operations.
 */
struct connection {
  explicit connection(int fd, cache& c, int executor_index = -1) :
      fd_(fd), c_(c), executor_index_(executor_index) {
    assert(fd_ != -1);
  }

  ~connection() {
    ::close(fd_);
  }

  /*!
   * Connecting socket.
   * This is used to write back responses.
   */
  int fd_ = 0;

  /*!
   * Cache reference.
   */
  cache& c_;

  /*!
   * Index of the assigned executor.
   */
  int executor_index_ = -1;

  /*!
   * \brief Buffer and validate the header.
   * @param b Incoming data buffer.
   * @return Return true if the packed was successfully buffered
   * and/or processed. False if the header was invalid,
   * which should be taken as an indication to close the session.
   */
  bool buffer_packet(buffer b);

private:
  /*!
   * \brief String for buffering incoming request.
   */
  std::string request_;

  /*!
   * \brief Header for the buffered request.
   */
  protocol_binary_request_header header_;

  /* Cache operations */
  bool handle_set();
  bool handle_get();
  bool handle_delete();

  /*!
   * \brief Process the packet.
   * @return True if packet was processed and request completed.
   * False if there was an error.
   */
  bool process_packet();

  /*!
   * \brief Write back response.
   * @param buf
   * @param len
   * @return
   */
  bool write_response(const unsigned char *buf, size_t len);

  /*!
   * \brief Write back error.
   * @param err error code.
   */
  void write_error(protocol_binary_response_status err);


  // Clear the buffered request.
  void reset() {
    memset(&header_, 0, sizeof(header_));
    request_.clear();
  }

  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;
};
}
