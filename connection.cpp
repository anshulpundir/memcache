#include "connection.h"
#include "util.h"
#include "protocol_binary.h"

#include <assert.h>
#include <unistd.h>
#include <iostream>
#include <sys/socket.h>
#include <string.h>
#include <algorithm>

namespace memcache {

bool connection::buffer_packet(buffer b) {
  if (b.empty())
    return true;

  // Check magic for new request.
  if (request_.empty()) {
    if (b[0] != PROTOCOL_BINARY_REQ) {
      return false;
    }
  }

  size_t prev_size = request_.size();

  // Buffer request.
  request_.insert(request_.end(), b.begin(), b.end());

  // Wait to receive header.
  if (request_.size() < sizeof(protocol_binary_request_header))
    return true;

  // Validate header.
  assert (prev_size < sizeof(header_));
  protocol_binary_request_header *h =
      (protocol_binary_request_header *) (&request_[0]);
  memcpy(&header_, h, sizeof(header_));

  header_.request.keylen = ntohs(h->request.keylen);
  header_.request.bodylen = ntohl(h->request.bodylen);
  header_.request.cas = ntohll(h->request.cas);

  protocol_binary_response_status status = util::validate_header(header_);
  if (status != PROTOCOL_BINARY_RESPONSE_SUCCESS) {
    write_error(status);
  }

  // Wait to receive complete packet.
  if (request_.size() < header_.request.bodylen + sizeof(header_)) {
    return true;
  } else if (request_.size() > header_.request.bodylen + sizeof(header_)) {
    // Received packet too big. Return error.
    write_error(PROTOCOL_BINARY_RESPONSE_EINVAL);
    return false;
  }

  return process_packet();
}

bool connection::handle_delete() {
  cache::value val(std::move(request_), header_);

  if (!c_.remove(val, header_.request.cas)) {
    write_error(PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS);
    return true;
  }

  //generate response
  buffer resp = util::build_response_hdr(header_, 0, 0);
  if (!write_response(resp.data(), resp.size())) {
    return false;
  }

  return true;
}

bool connection::process_packet() {
  bool ret = true;
  switch (header_.request.opcode) {
    case PROTOCOL_BINARY_CMD_SET:
      ret = handle_set();
      break;
    case PROTOCOL_BINARY_CMD_GET:
      ret = handle_get();
      break;
    case PROTOCOL_BINARY_CMD_DELETE:
      ret = handle_delete();
      break;
    default:
      write_error(PROTOCOL_BINARY_RESPONSE_UNKNOWN_COMMAND);
      break;
  }
  reset();
  return ret;
}

void connection::write_error(protocol_binary_response_status err) {
  const char *errstr = nullptr;

  switch (err) {
    case PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS:
      errstr = "Entry exists for key";
      break;
    case PROTOCOL_BINARY_RESPONSE_KEY_ENOENT:
      errstr = "Not found";
      break;
    case PROTOCOL_BINARY_RESPONSE_EINVAL:
      errstr = "Bad parameters";
      break;
    case PROTOCOL_BINARY_RESPONSE_UNKNOWN_COMMAND:
      errstr = "Unsupported command";
      break;
    case PROTOCOL_BINARY_RESPONSE_E2BIG:
      errstr = "Too large";
      break;
    default:
      assert(false);
      break;
  }

  size_t len = 0;
  if (errstr) {
    len = strlen(errstr);
  }
  buffer buf = util::build_response_hdr(header_, 0, len, err);
  if (len)
    buf.insert(buf.end(), errstr, errstr + len);

  write_response(&buf[0], buf.size());

  reset();
}

bool connection::write_response(const unsigned char *buf, size_t len) {
  while (len) {
    ssize_t cnt = ::write(fd_, buf, len);
    if (cnt == -1) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        std::cerr << "write error: fd=" << fd_ << " errno =" << errno << std::endl;
        return false;
      }
    } else {
      assert(cnt <= len);
      len -= cnt;
    }
  }
  return true;
}

bool connection::handle_set() {
  cache::value val(std::move(request_), header_);

  if (header_.request.cas) {
    if (!c_.cas(std::move(val), header_.request.cas)) {
      write_error(PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS);
      return true;
    }
  } else {
    c_.set(std::move(val));
  }

  //generate response
  buffer resp = util::build_response_hdr(header_, 0, 0);
  if (!write_response(resp.data(), resp.size())) {
    return false;
  }

  return true;
}

bool connection::handle_get() {
  typedef uint32_t flag_t;

  cache::value req(std::move(request_), header_);
  std::shared_ptr<cache::value> value = c_.get(req.get_key());

  if (!value) {
    write_error(PROTOCOL_BINARY_RESPONSE_KEY_ENOENT);
    return true;
  }

  // Construct response.
  flag_t f = 0;
  buffer hdr = util::build_response_hdr(header_, 0, value->packet_value_len() + sizeof(f),
                                        0, sizeof(f));
  hdr.insert(hdr.end(), (unsigned char *) &f, (unsigned char *) &f + sizeof(f));
  size_t size = value->packet_value_len();

  const char *buf = value->packet_user_data() + value->header_.request.keylen;
  hdr.insert(hdr.end(), buf, buf + size);

  // Write response.
  std::string res;
  res.assign((const char *) buf, value->packet_value_len());
  write_response(&hdr[0], hdr.size());

  return true;
}
}
