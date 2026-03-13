#include "TcpServer.hpp"
#include <thread>

namespace SimpleNet {

TcpServer::TcpServer(int port) {
    listen_socket_.bind(port);
    listen_socket_.listen();
}

void handle_client(Socket client, ClientHandler handler) {
    handler(std::move(client));
}

void TcpServer::run(ClientHandler handler) {

    while (true) {

        Socket client = listen_socket_.accept();

        std::thread(handle_client, std::move(client), handler).detach();
    }
}

}