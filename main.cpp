#include <asm-generic/socket.h>
#include <cstddef>
#include <cstring>
#include <expected>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

#define PORT "80"

using namespace std;
// using UniqueAddrInfo = unique_ptr<addrinfo, decltype([](addrinfo *p) {
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

class TCPConnect {
private:
  struct addrinfo hints;
  struct addrinfo *res = nullptr;
  Socket *sockfd = nullptr;

public:
  struct sockaddr_storage their_addr;
  TCPConnect(const char *port, bool opt, const char *adress = NULL) {
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (auto status = getaddrinfo(adress, port, &hints, &res); status != 0) {
      std::cerr << "getaddrinfo error: " << gai_strerror(status) << '\n';
      return;
    }
    // UniqueAddrInfo res(raw_res);
    sockfd = new Socket(res);

    if (opt) {
      int yes = 1;
      if (auto status = ::setsockopt(sockfd->get(), SOL_SOCKET, SO_REUSEADDR,
                                     reinterpret_cast<const void *>(&yes),
                                     static_cast<socklen_t>(sizeof(yes)));
          status < 0) {
        cerr << "setsockopt error: " << gai_strerror(status) << '\n';
      }
    }
    if (auto status = bind(sockfd->get(), res->ai_addr, res->ai_addrlen);
        status != 0) {
      cerr << "bind error: " << gai_strerror(status) << '\n';
    }
  }
  int get_socket() { return sockfd->get(); }
};

void recvall(int fd, void *buf) {
  // ToDo: сделать соединение открытым для нескольских запросов.
  while (recv(fd, buf, sizeof buf, 0) != 0) {
  }
}

int main() {
  TCPConnect *tcp_client = new TCPConnect(PORT, true);
  listen(tcp_client->get_socket(), 10);

  socklen_t client_addr_size = sizeof tcp_client->their_addr;
  auto client_fd =
      accept(tcp_client->get_socket(),
             (struct sockaddr *)&tcp_client->their_addr, &client_addr_size);

  char buf[1024];
  recvall(client_fd, buf);

  TCPConnect *tcp_host = new TCPConnect(PORT, true, "example.com");
  send(tcp_host->get_socket(), buf, sizeof buf, 0);

  close(client_fd);
}
