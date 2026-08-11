#include <asm-generic/socket.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <ostream>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <variant>

#define PORT "8080"

using namespace std;
// using UniqueAddrInfo = unique_ptr<addrinfo, decltype([](addrinfo *){
//                                      if (p)
//                                        freeaddrinfo(p);
//                                    })>;
class Socket {
private:
  int socketfd = -1;

public:
  Socket(addrinfo *res) {
    socketfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  }
  ~Socket() {
    if (socketfd >= 0)
      close(socketfd);
  }

  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;

  Socket(Socket &&other) noexcept : socketfd(other.socketfd) {
    other.socketfd = -1;
  }

  Socket &operator=(Socket &&other) noexcept {
    if (this != &other) {
      if (socketfd >= 0)
        close(socketfd);
      socketfd = other.socketfd;
      other.socketfd = -1;
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return socketfd; }
};

enum class Status { Ok, Disconect, Error, Eagain };

struct StringResult {
  string result;
  Status status;
};
struct DataResult {
  int data;
  Status status;
};
class TCP {
private:
  struct addrinfo hints;
  struct addrinfo *res = nullptr;
  Socket *sockfd = nullptr;

public:
  struct sockaddr_storage their_addr;
  TCP(const char *port, bool opt, bool host = false,
      const char *adress = NULL) {
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (host)
      hints.ai_flags = AI_PASSIVE;

    if (auto status = getaddrinfo(adress, port, &hints, &res); status != 0) {
      std::cerr << "getaddrinfo error: " << gai_strerror(status) << '\n';
      return;
    }
    // UniqueAddrInfo res(raw_res);
    sockfd = new Socket(res);

    if (opt) {
      int yes = 1;
      if (::setsockopt(sockfd->get(), SOL_SOCKET, SO_REUSEADDR,
                       reinterpret_cast<const void *>(&yes),
                       static_cast<socklen_t>(sizeof(yes))) < 0) {
        cerr << "setsockopt error: " << strerror(errno) << '\n';
      }
    }
  }
  void bind() {
    if (::bind(sockfd->get(), res->ai_addr, res->ai_addrlen) != 0) {
      cerr << "bind error: " << strerror(errno) << '\n';
    }
  }
  void connect() {
    if (::connect(sockfd->get(), res->ai_addr, res->ai_addrlen) == -1) {
      cerr << "connect error: " << strerror(errno) << '\n';
    }
  }
  int get_socket() { return sockfd->get(); }

  DataResult accept_fd(bool non_block) {
    socklen_t client_addr_size = sizeof their_addr;
    auto client_fd =
        accept(get_socket(), (struct sockaddr *)&their_addr, &client_addr_size);

    if (client_fd == -1) {
      if (errno == EAGAIN) {
        return {client_fd, Status::Eagain};
      }
      return {client_fd, Status::Error};
    }
    if (non_block)
      fcntl(client_fd, F_SETFL, O_NONBLOCK);

    return {client_fd, Status::Ok};
  }
};

class epoll_manage {
private:
  int epoll_fd;

  bool modify_epoll_event(int epoll_fd, int client_fd, uint32_t events,
                          int op = EPOLL_CTL_MOD) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events | EPOLLET;
    ev.data.fd = client_fd;

    if (epoll_ctl(epoll_fd, op, client_fd, &ev) == -1) {
      cerr << "epoll_ctl error: " << strerror(errno) << endl;
      return false;
    }
    return true;
  }

public:
  epoll_event epoll_ev[65];
  int epoll_create() {
    epoll_fd = ::epoll_create(69);
    if (epoll_fd == -1) {
      cerr << "epoll_create error: " << strerror(errno) << endl;
    }
    return epoll_fd;
  }

  inline bool epoll_add_read(int client_fd) {
    return modify_epoll_event(epoll_fd, client_fd, EPOLLIN, EPOLL_CTL_ADD);
  }
  inline bool epoll_enable_write(int client_fd) {
    return modify_epoll_event(epoll_fd, client_fd, EPOLLIN | EPOLLOUT,
                              EPOLL_CTL_MOD);
  }
  inline bool epoll_disable_write(int client_fd) {
    return modify_epoll_event(epoll_fd, client_fd, EPOLLIN, EPOLL_CTL_MOD);
  }
  inline bool epoll_remove(int client_fd) {
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr) == 0;
  }
};

StringResult recvall(int fd) {
  string request;
  char buf[1024];
  while (true) {
    auto len_recv = recv(fd, buf, sizeof(buf), 0);

    if (len_recv == 0) {
      return {request, Status::Disconect};
    }
    if (len_recv < 0) {
      if (errno == EAGAIN) {
        return {request, Status::Ok};
      }
      cerr << "recvall error: " << strerror(errno) << endl;
      return {request, Status::Error};
    }

    request.append(buf, len_recv);
    cout << request << endl;
  }
  return {request, Status::Ok};
}
size_t sendall(int fd, const string &request, size_t total_bytes) {
  auto bytes_left = request.size() - total_bytes;
  int n;
  while (total_bytes < request.size()) {
    n = send(fd, request.c_str() + total_bytes, bytes_left, MSG_NOSIGNAL);
    if (n == -1)
      break;
    total_bytes += n;
    bytes_left -= n;
    cout << total_bytes << endl;
  }
  return bytes_left;
}

atomic<bool> running{true};
void signal_handler(int sig) { running = false; }

int main() {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  TCP *tcp_host = new TCP("3490", true, true);
  tcp_host->bind();
  listen(tcp_host->get_socket(), 10);
  if (fcntl(tcp_host->get_socket(), F_SETFL, O_NONBLOCK) == -1) {
    cerr << "fctntl tcp_host error: " << strerror(errno) << endl;
  }
  epoll_manage *em = new epoll_manage();
  int ep_fd = em->epoll_create();
  auto ep_ev = em->epoll_ev;
  em->epoll_add_read(tcp_host->get_socket());

  string request = "";
  size_t remain_bytes = 0;
  cout << "Начинаю цикл" << endl;
  while (running) {
    int nfds = epoll_wait(ep_fd, ep_ev, 65, -1);
    if (nfds == -1) {
      if (errno == EINTR)
        continue;
      cerr << "epoll_wait error: " << strerror(errno) << endl;
      break;
    }

    for (int i = 0; i < nfds; i++) {
      int fd = ep_ev[i].data.fd;
      uint32_t revents = ep_ev[i].events;

      if (fd == tcp_host->get_socket()) {
        while (true) {
          auto [client_fd, status] = tcp_host->accept_fd(true);
          if (status != Status::Ok) {
            if (status == Status::Eagain) {
              break;
            } else
              cerr << "accept_fd error: " << strerror(errno) << endl;
            break;
          }
          em->epoll_add_read(client_fd);
        }
        continue;
      }
      if (revents & EPOLLIN) {
        auto [request, status] = recvall(fd);

        cout << "Принял данные: " << request << endl;
        if (!request.empty()) {
          if (remain_bytes = sendall(fd, request, 0); remain_bytes > 0) {
            em->epoll_enable_write(fd);
          }
        }
        if (status == Status::Disconect) {
          cout << "Клиент отключился." << endl << endl;
          em->epoll_remove(fd);
          close(fd);
          continue;
        }
      }

      if (revents & EPOLLOUT) {
        cout << "Доотправляю даннные: " << request << endl;
        remain_bytes = sendall(fd, request, remain_bytes);
        if (remain_bytes == 0) {
          request.clear();
          em->epoll_disable_write(fd);
        }
      }
    }
  }
}
