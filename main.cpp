#include "Server.hpp"
#include "Client.hpp"
#include <iostream>

void printUsage() {
    std::cout << "Usage:\n";
    std::cout << "  ./chat server\n";
    std::cout << "  ./chat client <host> <port> <username>\n";
    std::cout << "\nExample:\n";
    std::cout << "  ./chat server\n";
    std::cout << "  ./chat client 127.0.0.1 9000 Alice\n";
}
 
int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }
 
    std::string mode = argv[1];  // "server" или "client"
 
    try {
        if (mode == "server") {
            Chat::ChatServer server(9000);
            server.run();
 
        } else if (mode == "client") {
            if (argc != 5) {
                std::cerr << "[Error] Client requires: <host> <port> <username>\n";
                printUsage();
                return 1;
            }
 
            std::string host     = argv[2];
            int         port     = std::stoi(argv[3]);
            std::string username = argv[4];
 
            Chat::ChatClient client(host, port, username);
            client.run();
 
        } else {
            std::cerr << "[Error] Unknown mode: " << mode << "\n";
            printUsage();
            return 1;
        }
 
    } catch (const std::exception& e) {
        std::cerr << "[Fatal] " << e.what() << "\n";
        return 1;
    }
 
    return 0;
}
 