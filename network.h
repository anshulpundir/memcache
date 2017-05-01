//
// socket and epoll wrappers.
//

#pragma once

#include <vector>
#include <string>
#include <errno.h>
#include <sstream>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <iostream>

#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>

namespace memcache {

/*!
 * \brief Wrapper for addrinfo.
 */
struct address_info {
  /*!
   * brief list of items returned by getaddrinfo.
   */
  struct addrinfo *info_ = nullptr;

  address_info() {}

  ~address_info() {
    ::freeaddrinfo(info_);
  }

  /*!
   * \brief Init network addresses for given ip and port.
   * @return True if success. False otherwise.
   */
  bool init(const std::string& ip, int port) {
    struct addrinfo v;
    memset (&v, 0, sizeof(struct addrinfo));
    v.ai_family = AF_UNSPEC;
    v.ai_socktype = SOCK_STREAM;
    v.ai_flags = AI_PASSIVE;

    int err = ::getaddrinfo(!ip.empty()?ip.c_str():NULL, std::to_string(port).data(), &v, &info_);

    if (err) {
      std::cerr << "Unable to getaddrinfo: " << err;
      return false;
    }

    assert(info_);
    return true;
  }

  address_info(const address_info&) = delete;
  address_info& operator=(const address_info&) = delete;
};

/*!
 * brief Struct to hold FD for the incoming connections.
 */
struct connection_data {
  int fd_;
  std::string host_;
  std::string port_;
};

/*!
 * \brief Socket wrapper.
 * Used to listen for incoming data and used with epoll
 * IO event notifications.
 */
class socket {
public:
  explicit socket() :fd_(-1) {}

  ~socket() {
    ::close(fd_);
  }

  void set_non_blocking() {
    set_fcntl(fd_, O_NONBLOCK);
  }

  /*!
   * \brief Listen for connections.
   * @return
   */
  bool listen() {
    int err = ::listen(fd_, SOMAXCONN);
    return err != -1;
  }

  /*!
   * brief Bind the socket to the given ip and port.
   * @param ip
   * @param port
   * @return True if bind successful. False otherwise.
   */
  bool bind(const std::string& ip, int port) {
    struct linger lng = {0, 0};
    int flags = 1;

    // Get addresses and try to bind.
    address_info addr;
    bool ret = addr.init(ip, port);
    if (!ret) {
      return false;
    }

    for (struct addrinfo *info = addr.info_; info != nullptr; info = info->ai_next) {
      int fd = ::socket(info->ai_family, info->ai_socktype, info->ai_protocol);
      if (fd == -1) {
        continue;
      }

      // TCP
      int err = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *) &flags, sizeof(flags));
      if (err != 0) {
        std::cerr << "setsockopt error" << std::endl;
      }

      // Keep alive
      err = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *) &flags, sizeof(flags));
      if (err != 0) {
        std::cerr << "setsockopt error" << std::endl;
      }

      // Avoid large TIME_WAIT.
      err = setsockopt(fd, SOL_SOCKET, SO_LINGER, (void *) &lng, sizeof(lng));
      if (err != 0) {
        std::cerr << "setsockopt error" << std::endl;
      }

      // Bind.
      err = ::bind(fd, info->ai_addr, info->ai_addrlen);

      if (!err) {
        fd_ = fd;
        break;
      }

      ::close(fd);
    }

    if (fd_ == -1) {
      std::cerr << "Socket creation failed. Err: " << errno << std::endl;
      return false;
    }

    set_non_blocking();
    return true;
  }

  /*!
   * \brief Accept a connection on this socket and return the incoming
   * socket info.
   * @param cd incoming socket info.
   * @return True if successful without errors.
   */
  bool connect(connection_data *cd) {
    struct sockaddr addr;
    socklen_t len = sizeof(addr);
    cd->fd_ = accept(fd_, &addr, &len);

    if (cd->fd_ == -1)  {
      if ( (errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        return false;
      } else {
        std::cerr << "incoming connection error" << std::endl;
        return false;
      }
    }

    char hbuf[NI_MAXHOST];
    char sbuf[NI_MAXSERV];

    int err = ::getnameinfo(&addr, len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                            NI_NUMERICHOST | NI_NUMERICSERV);

    if (err) {
      std::cerr << "getnameinfo error" << std::endl;
      return false;
    }

    cd->host_ = std::string(hbuf);
    cd->port_ = std::string(sbuf);
    set_fcntl(cd->fd_, O_NONBLOCK);

    return true;
  }

  int fd() {
    return fd_;
  }

private:
  /*!
   * \brief Socket to listen for incoming connections.
   */
  int fd_;

  socket(const socket&) = delete;
  socket& operator=(const socket&) = delete;

  static bool set_fcntl(int fd, int flags) {
    int f = ::fcntl(fd, F_GETFL, 0);
    if (f == -1) {
      std::cerr << "fcntl error, GETFL" << std::endl;
      return false;
    }

    f |= flags;
    f = ::fcntl(fd, F_SETFL, f);
    if (f == -1) {
      std::cerr << "fcntl error, SETFL" << std::endl;
      return false;
    }
  }
};

/*!
 * brief Epoll wrapper.
 */
class EpollHelper {
public:
  explicit EpollHelper(int max_events) {
    events_.resize(max_events);
    for (auto& v : events_) {
      v.data.ptr = nullptr;
    }
  }

  ~EpollHelper() {
    ::close(fd_);
  }

  /*!
   * \brief Create epoll instance.
   * @return
   */
  bool open() {
    fd_ = epoll_create1(0);
    return fd_ != -1;
  };

  /*!
   * brief Add the socket to be watched on the epoll isntance.
   * @param s Socket to be watched.
   * @return True if successful.
   */
  bool listen_socket(socket& s);

  /*!
   * brief Add the given descriptor to be watched on the epoll.
   * Used after accepting a new incoming connection.
   * @param fd
   * @param user
   * @return True if successful without errors.
   */
  bool add_descriptor(int fd, void* user);

  /*!
   * \brief Wait for events on the watched sockets.
   * @return Number of file descriptors ready for I/O.
   */
  int wait();

  std::vector<epoll_event> events_;
private:
  int fd_;
  EpollHelper(const EpollHelper&) = delete;
  EpollHelper& operator=(const EpollHelper&) = delete;
};

}