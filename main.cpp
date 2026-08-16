#include "network.hpp"
#include <asm-generic/socket.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
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
#include <vector>

#define PORT "8080"

using namespace std;

Socket create_tcp_upstream() {
  auto upstream =
      TCPserver::create_tcp("127.127.1.1", "3491", SocketMode::Connector, true);
  if (!upstream) {
    cerr << "Не удалось создать tcp upstream";
  }
  return upstream->get_socket();
}

atomic<bool> running{true};
void signal_handler(int sig) { running = false; }

void eventloop(TCPserver &server, EpollManage &epoll) {
  int ep_fd = epoll.epoll_create();

  auto ep_ev = epoll.epoll_ev;

  Connection listener_conn{.socket = server.get_socket(), .peer = nullptr};

  epoll.epoll_add_read(&listener_conn);
  std::unordered_map<int, std::unique_ptr<Session>> sessions;
  unordered_map<int, ClientSession> active_clients;

  vector<int> erase_list;
  while (running) {
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

      if (conn->socket.get() == -1) {
        continue;
      }

      if (conn->peer == nullptr) {
        while (true) {
          auto [client, status] = accept_fd(conn->socket.get(), true);
          if (status != Status::Ok) {
            if (status == Status::Eagain) {
              break;
            } else {
              cerr << "accept_fd error: " << strerror(errno) << endl;
            }
            break;
          }
          auto client_fd = client.get();

          auto upstream = create_tcp_upstream();
          cout << "upstream " << upstream.get() << endl;
          auto session = std::make_unique<Session>();
          session->client.socket = std::move(client);
          session->upstream.socket = std::move(upstream);

          epoll.epoll_add_read(&session->client);
          epoll.epoll_add_read(&session->upstream);
          cout << "client_fd " << client_fd << endl;
          sessions[client_fd] = move(session);
          cout << client_fd << endl;
        }
        continue;
      } else {
        if (revents & EPOLLIN) {
          auto [request, status] = recvall(conn->socket.get());
          if (!request.empty()) {
            conn->peer->out_buffer = request;
            cout << "Записано " << conn->peer->out_buffer << endl;
            epoll.epoll_enable_write(conn->peer);
          }
          if (status == Status::Disconect) {
            cout << "ЗАКРЫВАЕМ ВСЁ НАХУЙ" << conn->socket.get() << endl;
            auto client_fd = conn->socket.get();
            epoll.epoll_remove(conn);
            if (conn->peer)
              epoll.epoll_remove(conn->peer);

            conn->socket = Socket(-1);
            if (conn->peer)
              conn->peer->socket = Socket(-1);

            erase_list.push_back(client_fd);
          }
          continue;
        }
      }
      if (revents & EPOLLOUT) {
        auto status = sendall(conn->socket.get(), conn->out_buffer);
        if (status == Status::Error) {
          cerr << "sendall error: " << strerror(errno) << endl;
        }
        if (conn->out_buffer.empty()) {
          cout << "Отправил" << endl;
          conn->out_buffer.clear();
          epoll.epoll_disable_write(conn);
        }
      }
    }
    for (int key : erase_list) {
      sessions.erase(key);
    }
    erase_list.clear();
  }
}

int main() {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  if (auto server =
          TCPserver::create_tcp(NULL, "3490", SocketMode::Listener, true)) {

    EpollManage epoll;
    cout << "Начинаю цикл" << endl;

    eventloop(*server, epoll);
  } else {
    return 1;
  }
}
