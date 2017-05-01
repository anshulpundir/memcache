
#include "network.h"

using namespace memcache;

bool EpollHelper::listen_socket(memcache::socket& s) {
  struct epoll_event event;
  event.data.ptr = (void* ) &s;
  event.events = EPOLLIN | EPOLLET;

  // Associate epoll fd with the socket fd.
  int err = epoll_ctl(fd_, EPOLL_CTL_ADD, s.fd(), &event);

  if (err == -1) {
    std::cerr << "Epoll listen error, epoll fd: " << fd_ << " sock fd: " << s.fd();
    return false;
  }

  s.listen();
}

bool EpollHelper::add_descriptor(int fd, void* user) {
  struct epoll_event event;
  event.data.ptr = user;
  event.events = EPOLLOUT | EPOLLIN | EPOLLET;

  // Add given fd to the watched set.
  int err = epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &event);

  if (err == -1) {
    std::cerr << "Error adding to epool, fd: " << fd << std::endl;
    return false;
  }

  return true;
}

int EpollHelper::wait() {
  int n = ::epoll_wait(fd_, &events_.at(0), events_.size(), -1);
  if (n == -1) {
    std::cerr << "Epoll wait error, fd: " << fd_;
    return -1;
  }

  return n;
}