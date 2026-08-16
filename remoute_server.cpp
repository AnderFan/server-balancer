#include <asm-generic/socket.h>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <iostream>
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

struct Client {
  Socket socket;
  string read_buffer;
  string write_buffer;
};

void eventloop(TCPserver &server, EpollManage &epoll) {
  int ep_fd = epoll.epoll_create();
  auto ep_ev = epoll.epoll_ev;

  Connection listener_conn{.socket = server.get_socket(), .peer = nullptr};
  epoll.epoll_add_read(&listener_conn);
  unordered_map<int, Connection> sessions;

  while (true) {
    int nfds = epoll_wait(ep_fd, ep_ev, 65, -1);
    if (nfds == -1) {
      if (errno == EINTR)
        continue;
      cerr << "epoll_wait error: " << strerror(errno) << endl;
      break;
    }

    for (int i = 0; i < nfds; i++) {
      auto *conn = static_cast<Connection *>(ep_ev[i].data.ptr);
      uint32_t revents = ep_ev[i].events;

      if (conn->peer == nullptr) {
        while (true) {
          auto [client, status] = accept_fd(conn->socket.get(), true);
          if (status != Status::Ok) {
            if (status == Status::Eagain) {
              break;
            } else {
              cout << conn->socket.get() << endl;
              cerr << "accept_fd error: " << strerror(errno) << endl;
            }
            break;
          }
          Connection session;
          int client_fd = client.get();
          session.socket = std::move(client);

          sessions[client_fd] = move(session);
          epoll.epoll_add_read(&sessions[client_fd]);
        }
        continue;
      } else {
        if (revents & EPOLLIN) {
          auto [request, status] = recvall(conn->socket.get());
          if (request.size() > 2) { // Убираем \n
            request.erase(request.size() - 2);
          }
          cout << "Принял данные от клиента: " << request << endl;
          if (!request.empty()) {
            conn->out_buffer = request + "-Siroyeska\n";
            epoll.epoll_enable_write(conn);
          }

          if (status == Status::Disconect) {
            cout << "Клиент отключился." << endl << endl;
            epoll.epoll_remove(conn);
            close(conn->socket.get());
            sessions.erase(conn->socket.get());
            continue;
          }
        }

        if (revents & EPOLLOUT) {
          auto status = sendall(conn->socket.get(), conn->out_buffer);

          if (status == Status::Error) {
            cerr << "sendall error: " << strerror(errno) << endl;
          }
          if (conn->out_buffer.empty()) {
            cout << "Отправил сыроежку" << endl;
            conn->out_buffer.clear();
            epoll.epoll_disable_write(conn);
          }
        }
      }
    }
  }
}

int main() {
  if (auto server = TCPserver::create_tcp("127.127.1.1", "3491",
                                          SocketMode::Listener, true)) {
    EpollManage epoll;
    eventloop(*server, epoll);
  } else {
    cerr << "Ошибка: " << strerror(errno) << endl;
    return 1;
  }
}
