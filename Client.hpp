#pragma once
 
#include "Socket.hpp"
#include "Protocol.hpp"
#include <string>
#include <atomic>

namespace Chat {
 
class ChatClient {
public:
    ChatClient(const std::string& host, int port, const std::string& username);
    void run();
 
private:
    void receiveLoop();
    void sendLoop();
 
    bool parseInput(const std::string& input, Protocol::Message& out);
    void printMessage(const Protocol::Message& msg);
 
    std::string host_;
    int port_;
    std::string username_;
 
    SimpleNet::Socket socket_;
 
    // Флаг остановки — когда сервер отключается, оба потока завершаются
    std::atomic<bool> running_;
};
 
} // namespace Chat
 