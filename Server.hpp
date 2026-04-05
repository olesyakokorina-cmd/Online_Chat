#pragma once
 
#include "TcpServer.hpp"
#include "Protocol.hpp"
 
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace Chat {
 
struct ClientInfo {
    std::string username;
    std::shared_ptr<SimpleNet::Socket> socket;
};
 
class ChatServer {
public:
    explicit ChatServer(int port);
    void run();
 
private:
    void handleClient(SimpleNet::Socket client);
 
    bool handleAuth(const Protocol::Message& msg,
                    std::shared_ptr<SimpleNet::Socket> sock);
 
    void handleText(const Protocol::Message& msg);
 
    void broadcast(const Protocol::Message& msg);
 
    bool sendTo(const std::string& username, const Protocol::Message& msg);
 
    std::unordered_map<std::string, ClientInfo> clients_;
    std::mutex clients_mutex_;

    std::unordered_map<std::string, std::string> userDatabase_; 
    std::mutex userdb_mutex_; 
 
    SimpleNet::TcpServer tcp_server_;
};
 
} // namespace Chat
 