#pragma once

#include "Socket.hpp"
#include <functional>

namespace SimpleNet {

using ClientHandler = std::function<void(Socket)>;

class TcpServer {
public:
    TcpServer(int port);

    void run(ClientHandler handler);

private:
    Socket listen_socket_;
};

}