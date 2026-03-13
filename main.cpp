#include "TcpServer.hpp"
#include <iostream>

using namespace SimpleNet;

int main() {

    TcpServer server(8080);

    server.run([](Socket client) {

        auto data = client.receive();

        std::string msg(data.begin(), data.end());

        std::cout << "Client: " << msg << std::endl;

        client.send("Hello");

    });

    return 0;
}