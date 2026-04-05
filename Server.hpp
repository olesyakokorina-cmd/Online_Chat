#pragma once
 
#include "TcpServer.hpp"
#include "Protocol.hpp"
 
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
 
// ════════════════════════════════════════════════════════════════
//  Server.hpp — Бизнес-логика чат-сервера
//
//  TcpServer (твой код) — сетевой слой:
//    принимает соединения, создаёт поток на каждого клиента
//
//  ChatServer (наш код) — логика:
//    хранит список клиентов, раздаёт сообщения
// ════════════════════════════════════════════════════════════════
 
namespace Chat {
 
// данные об одном подключённом клиенте
struct ClientInfo {
    std::string username;
    std::shared_ptr<SimpleNet::Socket> socket;
};
 
class ChatServer {
public:
    explicit ChatServer(int port);
    void run();
 
private:
    // Вызывается в отдельном потоке для каждого клиента
    void handleClient(SimpleNet::Socket client);
 
    // Регистрирует клиента по имени, возвращает false если имя занято
    bool handleAuth(const Protocol::Message& msg,
                    std::shared_ptr<SimpleNet::Socket> sock);
 
    // Обрабатывает текстовое сообщение (/all или /dm)
    void handleText(const Protocol::Message& msg);
 
    // Отправляет сообщение всем клиентам
    void broadcast(const Protocol::Message& msg);
 
    // Отправляет сообщение одному клиенту по имени
    // Возвращает false если клиент не найден
    bool sendTo(const std::string& username, const Protocol::Message& msg);
 
    // Список клиентов: имя → ClientInfo
    std::unordered_map<std::string, ClientInfo> clients_;
 
    // Мьютекс защищает clients_ от одновременного доступа из разных потоков
    std::mutex clients_mutex_;
 
    SimpleNet::TcpServer tcp_server_;
};
 
} // namespace Chat
 