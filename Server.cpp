#include "Server.hpp"
#include <iostream>
 
namespace Chat {

ChatServer::ChatServer(int port)
    : tcp_server_(port)
{
    std::cout << "[Server] Started on port " << port << "\n";

    sqlite3_open("../data/chat.db", &db_); // открывает (или создаёт, если не существует) файл базы данных chat.db и сохраняет указатель на базу данных в переменной db_

    if (sqlite3_open("../data/chat.db", &db_) != SQLITE_OK) { //SQLITE_OK == 0
        std::cerr << "Cannot open DB\n";
    }

    const char* sql = R"( 
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE,
            password TEXT
        );
    )";

    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "SQL error: " << err << "\n";
        sqlite3_free(err);
    }
}

ChatServer::~ChatServer() {
    if (db_) {
        sqlite3_close(db_);
    }
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
 
    bool ok = false;

    if (auth_msg.type == Protocol::MessageType::LOGIN) {
        ok = handleLogin(auth_msg, sock);
    }
    else if (auth_msg.type == Protocol::MessageType::REGISTER) {
        ok = handleRegister(auth_msg, sock);
    }
    else {
        Protocol::sendMessage(*sock,
            Protocol::makeError("First message must be LOGIN or REGISTER"));
        return;
    }

    if (!ok) {
        return;
    }
 
    std::string username = auth_msg.from;
 
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

bool ChatServer::handleLogin(const Protocol::Message& msg,
                             std::shared_ptr<SimpleNet::Socket> sock) {

    std::string username = msg.from;
    std::string password = msg.body;

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);

        if (clients_.count(username)) {
            Protocol::sendMessage(*sock,
                Protocol::makeError("User already online"));

            return false;
        }
    }

    std::lock_guard<std::mutex> db_lock(db_mutex_);

    sqlite3_stmt* stmt;

    const char* sql =
        "SELECT password FROM users WHERE username = ?;";

    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1,
        username.c_str(),
        -1,
        SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {

        sqlite3_finalize(stmt);

        Protocol::sendMessage(*sock,
            Protocol::makeError("User not found"));

        return false;
    }

    std::string db_pass =
        reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 0));

    sqlite3_finalize(stmt);

    if (db_pass != password) {

        Protocol::sendMessage(*sock,
            Protocol::makeError("Wrong password"));

        return false;
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);

        clients_[username] =
            ClientInfo{ username, sock };
    }

    Protocol::sendMessage(*sock,
        Protocol::makeSystem("Welcome, " + username + "!"));

    return true;
}

bool ChatServer::handleRegister(const Protocol::Message& msg,
                                std::shared_ptr<SimpleNet::Socket> sock) {

    std::string username = msg.from;
    std::string password = msg.body;

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);

        if (clients_.count(username)) {

            Protocol::sendMessage(*sock,
                Protocol::makeError("User already online"));

            return false;
        }
    }

    std::lock_guard<std::mutex> db_lock(db_mutex_);

    sqlite3_stmt* stmt;

    const char* check_sql =
        "SELECT 1 FROM users WHERE username = ?;";

    sqlite3_prepare_v2(db_, check_sql,
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1,
        username.c_str(),
        -1,
        SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {

        sqlite3_finalize(stmt);

        Protocol::sendMessage(*sock,
            Protocol::makeError("User already exists"));

        return false;
    }

    sqlite3_finalize(stmt);

    const char* insert_sql =
        "INSERT INTO users(username, password) VALUES(?, ?);";

    sqlite3_prepare_v2(db_, insert_sql,
        -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1,
        username.c_str(),
        -1,
        SQLITE_TRANSIENT);

    sqlite3_bind_text(stmt, 2,
        password.c_str(),
        -1,
        SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {

        sqlite3_finalize(stmt);

        Protocol::sendMessage(*sock,
            Protocol::makeError("DB error"));

        return false;
    }

    sqlite3_finalize(stmt);

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);

        clients_[username] =
            ClientInfo{ username, sock };
    }

    Protocol::sendMessage(*sock,
        Protocol::makeSystem(
            "Registered and logged in as " + username));

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
 
