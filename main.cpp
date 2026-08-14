#include "network.hpp"
#include <algorithm>
#include <asm-generic/socket.h>
#include <atomic>
#include <cerrno>
#include <charconv>
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
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#define PORT "8080"

using namespace std;

atomic<bool> running{true};
void signal_handler(int sig) { running = false; }

void eventloop(TCPserver &server, EpollManage &epoll, TCPserver &remoute_host) {
  int ep_fd = epoll.epoll_create();
  auto ep_ev = epoll.epoll_ev;

  int server_fd = server.get_fd();
  epoll.epoll_add_read(server_fd);

  int host_fd = remoute_host.get_fd();
  epoll.epoll_add_read(host_fd);

  unordered_map<int, ClientSession> active_clients;
  deque<SendData> queue_to_send_remoute;
  vector<SendData> queue_to_send_client;
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

      if (fd == server_fd && fd != host_fd) {
        while (true) {
          auto [client_fd, status] = server.accept_fd(true);
          if (status != Status::Ok) {
            if (status == Status::Eagain) {
              break;
            } else {
              cerr << "accept_fd error: " << strerror(errno) << endl;
            }
            break;
          }
          active_clients[client_fd] = ClientSession{.fd = client_fd};
          epoll.epoll_add_read(client_fd);
        }
        continue;
      }

      auto &client = active_clients[fd];

      if (revents & EPOLLIN) {
        auto [request, status] = recvall(fd, false);

        if (fd != host_fd) {
          client.read_buffer = request;

          cout << "Принял данные от клиента: " << request << endl;
          if (!request.empty()) {
            queue_to_send_remoute.push_back({::move(client.read_buffer), fd});
            epoll.epoll_enable_write(host_fd);
          }
          for (auto a : queue_to_send_remoute) {
            cout << a.fd << " " << a.str << endl;
          }
          if (status == Status::Disconect) {
            cout << "Клиент отключился." << endl << endl;
            epoll.epoll_remove(fd);
            std::erase_if(queue_to_send_remoute,
                          [fd](const SendData &item) { return item.fd == fd; });
            close(fd);
            continue;
          }
        }
        if (fd == host_fd) {
          auto [bufer, c_fd] = parse_string(request);
          active_clients[c_fd].write_buffer = bufer;
        }
      }

      if (revents & EPOLLOUT) {

        if (fd == host_fd) {
          cout << "Отправлюя данные на сервер" << endl;
          while (!queue_to_send_remoute.empty()) {
            auto &client = queue_to_send_remoute.front();

            string bufer = to_string(client.fd) + ":" + client.str + "\n";

            auto status = sendall(host_fd, bufer);
            if (status == Status::Error) {
              cerr << "sendall error: " << strerror(errno) << endl;
            }
            if (status == Status::Eagain) {
              break;
              cout << "Буфер полный. Ждём" << endl;
            }
            queue_to_send_remoute.pop_front();
          }
          if (queue_to_send_remoute.empty()) {
            cout << "Всё отпрвили" << endl;
            epoll.epoll_disable_write(host_fd);
          }
        }
        if (fd != host_fd) {
          auto status = sendall(fd, client.write_buffer);
          if (status == Status::Error) {
            cerr << "sendall error: " << strerror(errno) << endl;
          }
          if (client.bytes_left == 0) {
            client.write_buffer.clear();
            epoll.epoll_disable_write(fd);
          }
        }
      }
    }
  }
}

int main() {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  auto remoute_host =
      TCPserver::create_tcp("127.127.1.1", "3491", SocketMode::Connector);
  if (remoute_host == nullopt) {
    cerr << "Не удалось подключиться к серверу" << endl;
    return 1;
  }
  if (auto server = TCPserver::create_tcp(NULL, "3490", SocketMode::Listener)) {

    if (fcntl(server->get_fd(), F_SETFL, O_NONBLOCK) == -1) {
      cerr << "fctntl tcp_host error: " << strerror(errno) << endl;
    }
    EpollManage epoll;
    cout << "Начинаю цикл" << endl;

    eventloop(*server, epoll, *remoute_host);
  } else {
    return 1;
  }
}
