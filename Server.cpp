#include "Server.hpp"
#include <iostream>
 
namespace Chat {

ChatServer::ChatServer(int port)
    : tcp_server_(port)
{
    std::cout << "[Server] Started on port " << port << "\n";
}

void ChatServer::run() {
    tcp_server_.run([this](SimpleNet::Socket client) {
        handleClient(std::move(client));
    });
}
 
//  handleClient() — жизненный цикл одного клиента
void ChatServer::handleClient(SimpleNet::Socket client) {
    auto sock = std::make_shared<SimpleNet::Socket>(std::move(client));
 
    Protocol::Message auth_msg;
    if (!Protocol::recvMessage(*sock, auth_msg)) {
        return;  
    }
 
    if (auth_msg.type != Protocol::MessageType::AUTH) {
        Protocol::sendMessage(*sock, Protocol::makeError("First message must be AUTH"));
        return;
    }
 
    std::string username = auth_msg.from;
 
    if (!handleAuth(auth_msg, sock)) {
        return;  
    }
 
    std::cout << "[Server] " << username << " connected\n";
 
    broadcast(Protocol::makeSystem(username + " joined the chat"));
 
    while (true) {
        Protocol::Message msg;
 
        if (!Protocol::recvMessage(*sock, msg)) {
            break;
        }
 
        switch (msg.type) {
            case Protocol::MessageType::TEXT:
                handleText(msg);
                break;
            default:
                break;
        }
    }
 
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(username);
    }
 
    std::cout << "[Server] " << username << " disconnected\n";
    broadcast(Protocol::makeSystem(username + " left the chat"));
}

//  handleAuth() — регистрация клиента
bool ChatServer::handleAuth(const Protocol::Message& msg,
                            std::shared_ptr<SimpleNet::Socket> sock) {
    std::string username = msg.from;
    std::string password = msg.body;

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        if (clients_.count(username)) {
            Protocol::sendMessage(*sock, Protocol::makeError("Username '" + username + "' is already connected"));
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(userdb_mutex_);
        auto it = userDatabase_.find(username);

        if (it != userDatabase.end()) {
            if (it->second != password) {
                Protocol::sendMessage(*sock, Protocol::makeError("Wrong password for user '" + username + "'"));
                return false;
            }
        } else {
            userDatabase_[username] = password;
        }
    }

    // Добавляем в список подключённых
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_[username] = ClientInfo{ username, sock };
    }

    Protocol::sendMessage(*sock,
        Protocol::makeSystem("Welcome, " + username + "! Commands: /all <text> or /dm <name> <text>"));
    return true;
}
 
//  handleText() — обработка текстового сообщения
void ChatServer::handleText(const Protocol::Message& msg) {
    if (msg.to == "ALL") {
        broadcast(msg);
    } else {
        bool found = sendTo(msg.to, msg);
 
        if (!found) {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            if (clients_.count(msg.from)) {
                Protocol::sendMessage(*clients_[msg.from].socket,
                    Protocol::makeError("User '" + msg.to + "' not found"));
            }
        } else {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            if (clients_.count(msg.from)) {
                Protocol::sendMessage(*clients_[msg.from].socket, msg);
            }
        }
    }
}

void ChatServer::broadcast(const Protocol::Message& msg) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
 
    for (auto& [name, info] : clients_) {
        try {
            Protocol::sendMessage(*info.socket, msg);
        } catch (...) {
            // Клиент отключился — пропускаем
        }
    }
}

bool ChatServer::sendTo(const std::string& username, const Protocol::Message& msg) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
 
    auto it = clients_.find(username);
    if (it == clients_.end()) {
        return false;
    }
 
    try {
        Protocol::sendMessage(*it->second.socket, msg);
    } catch (...) {
        // Клиент отключился
    }
 
    return true;
}
 
} // namespace Chat
 
