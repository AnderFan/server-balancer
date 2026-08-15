#include <asm-generic/socket.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <optional>
#include <ostream>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

#include "network.hpp"
using namespace std;
void eventloop(TCPserver &server) {
  if (listen(server.get_fd(), 69) == -1) {
    cerr << "listen error: " << strerror(errno) << endl;
  }
  for (;;) {
    auto [proxy_fd, status_accept] = server.accept_fd(false);
    if (status_accept != Status::Ok) {
      cerr << "accept_fd error: " << strerror(errno) << endl;
      return;
    }
    cout << "Коннект есть!" << endl;
    for (;;) {
      auto [request, status_revcall] = recvall(proxy_fd);
      if (status_revcall == Status::Disconect) {
        close(proxy_fd);
        break;
      }
      SendData data = parse_string(request);

      if (status_revcall == Status::Error) {
        cerr << "recvall error :" << strerror(errno) << endl;
      }
      string bufer = to_string(data.fd) + ":" + data.str + "_Capybara" + "\n";
      cout << request << endl;
      while (true) {
        auto status_sendall = sendall(proxy_fd, bufer);
        if (status_sendall == Status::Error) {
          cerr << "sendall error: " << strerror(errno) << endl;
          return;
        }
        cout << "Отправил данные" << endl;
        break;
      }
    }
  }
}

int main() {
  if (auto server =
          TCPserver::create_tcp("127.127.1.1", "3491", SocketMode::Listener)) {
    eventloop(*server);
  } else {
    return 1;
  }
}
