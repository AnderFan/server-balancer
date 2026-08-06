#include <asm-generic/socket.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <expected>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

#define PORT "8080"

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
};

string recvall(int fd) {
  string request;
  char buf[1024];
  while (true) {
    ssize_t rec = recv(fd, buf, sizeof(buf) - 1, 0);
    if (rec <= 0) {
      break;
    }

    buf[rec] = '\0';
    request.append(buf, rec);

    if (request.find("\r\n\r\n") != string::npos) {
      break;
    }
  }
  return request;
}

int main() {
  TCP *tcp_host = new TCP(PORT, true, true);
  tcp_host->bind();
  listen(tcp_host->get_socket(), 10);
  socklen_t client_addr_size = sizeof tcp_host->their_addr;
  auto client_fd =
      accept(tcp_host->get_socket(), (struct sockaddr *)&tcp_host->their_addr,
             &client_addr_size);
  cout << "Контакт есть" << endl;
  string request = recvall(client_fd);
  cout << "Данные получены " << request << endl;

  TCP *tcp_client = new TCP("80", true, false, "webhook.site");
  tcp_client->connect();
  if (send(tcp_client->get_socket(), request.c_str(), request.size(), 0) ==
      -1) {
    cerr << "send erorr: " << strerror(errno) << endl;
  }
  cout << "Данные отправлены" << endl;

  char res_buf[1024];
  ssize_t byres_read;
  while ((byres_read = (recv(tcp_client->get_socket(), res_buf,
                             sizeof(res_buf) - 1, 0)))) {
    res_buf[byres_read] = '\0';
    cout << res_buf << endl;

    if (send(client_fd, res_buf, byres_read, 0) == -1) {
      cerr << "send to console error: " << strerror(errno) << endl;
    };
  }

  close(client_fd);
}
