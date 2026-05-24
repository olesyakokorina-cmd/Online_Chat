#include "Server.hpp"
#include "Crypto.hpp"
#include <iostream>
 
namespace Chat {

ChatServer::ChatServer(int port)
    : tcp_server_(port)
{
    std::cout << "[Server] Started on port " << port << "\n";

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

    const char* rooms_sql = R"(
        CREATE TABLE IF NOT EXISTS rooms (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            is_private INTEGER NOT NULL DEFAULT 0,
            password TEXT
        );
    )";

    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "SQL error: " << err << "\n";
        sqlite3_free(err);
    }

    char* rooms_err = nullptr;
    if (sqlite3_exec(db_, rooms_sql, nullptr, nullptr, &rooms_err) != SQLITE_OK) {
        std::cerr << "Rooms SQL error: " << rooms_err << "\n";
        sqlite3_free(rooms_err);
    }

    const char* default_room_sql = R"(
        INSERT OR IGNORE INTO rooms(name, is_private, password)
        VALUES ('general', 0, NULL);
    )";

    char* default_room_err = nullptr;
    if (sqlite3_exec(db_, default_room_sql, nullptr, nullptr, &default_room_err) != SQLITE_OK) {
        std::cerr << "Default room SQL error: " << default_room_err << "\n";
        sqlite3_free(default_room_err);
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
            case Protocol::MessageType::JOIN:
                handleJoin(msg);
                break;
            case Protocol::MessageType::ROOMS:
                handleRooms(sock);
                break;
            case Protocol::MessageType::CREATE_ROOM:
                handleCreateRoom(msg, false);
                break;

            case Protocol::MessageType::CREATE_PRIVATE_ROOM:
                handleCreateRoom(msg, true);
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

    password = sha256(password);

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
            ClientInfo{ username, "general", sock };
    }

    Protocol::sendMessage(*sock,
        Protocol::makeSystem("Welcome, " + username + "!"));

    return true;
}

bool ChatServer::handleRegister(const Protocol::Message& msg,
                                std::shared_ptr<SimpleNet::Socket> sock) {

    std::string username = msg.from;
    std::string password = msg.body;

    password = sha256(password);

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
            ClientInfo{ username, "general", sock };
    }

    Protocol::sendMessage(*sock,
        Protocol::makeSystem(
            "Registered and logged in as " + username));

    return true;
}

void ChatServer::handleJoin(const Protocol::Message& msg) {
    std::string username = msg.from;
    std::string room = msg.room;
    std::string code = msg.body;

    std::lock_guard<std::mutex> db_lock(db_mutex_);

    sqlite3_stmt* stmt;

    const char* sql =
        "SELECT is_private, password FROM rooms WHERE name = ?;";

    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1,
        room.c_str(),
        -1,
        SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);

        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(username);

        if (it != clients_.end()) {
            Protocol::sendMessage(
                *it->second.socket,
                Protocol::makeError("Room not found: " + room)
            );
        }

        return;
    }

    int is_private = sqlite3_column_int(stmt, 0);

    const unsigned char* db_code_raw =
        sqlite3_column_text(stmt, 1);

    std::string db_code =
        db_code_raw ? reinterpret_cast<const char*>(db_code_raw) : "";

    sqlite3_finalize(stmt);

    if (is_private && db_code != code) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(username);

        if (it != clients_.end()) {
            Protocol::sendMessage(
                *it->second.socket,
                Protocol::makeError("Wrong room code")
            );
        }

        return;
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);

        auto it = clients_.find(username);
        if (it == clients_.end()) {
            return;
        }

        it->second.room = room;

        Protocol::sendMessage(
            *it->second.socket,
            Protocol::makeSystem("You joined room: " + room)
        );
    }
}

void ChatServer::handleRooms(
    std::shared_ptr<SimpleNet::Socket> sock) {

    std::lock_guard<std::mutex> db_lock(db_mutex_);

    sqlite3_stmt* stmt;

    const char* sql =
        "SELECT name, is_private FROM rooms;";

    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    std::string result = "Rooms:\n";

    while (sqlite3_step(stmt) == SQLITE_ROW) {

        std::string room =
            reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 0));

        int is_private =
            sqlite3_column_int(stmt, 1);

        result += "- " + room;

        if (is_private) {
            result += " (private)";
        }

        result += "\n";
    }

    sqlite3_finalize(stmt);

    Protocol::sendMessage(
        *sock,
        Protocol::makeSystem(result)
    );
}

void ChatServer::handleCreateRoom(const Protocol::Message& msg, bool isPrivate) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    sqlite3_stmt* stmt;

    const char* sql =
        "INSERT INTO rooms(name, is_private, password) VALUES(?, ?, ?);";

    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, msg.room.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, isPrivate ? 1 : 0);

    if (isPrivate) {
        sqlite3_bind_text(stmt, 3, msg.body.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 3);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);

        auto it = clients_.find(msg.from);
        if (it != clients_.end()) {
            Protocol::sendMessage(
                *it->second.socket,
                Protocol::makeError("Room already exists or DB error")
            );
        }

        return;
    }

    sqlite3_finalize(stmt);

    auto it = clients_.find(msg.from);
    if (it != clients_.end()) {
        Protocol::sendMessage(
            *it->second.socket,
            Protocol::makeSystem("Room created: " + msg.room)
        );
    }
}

//  handleText() — обработка текстового сообщения
void ChatServer::handleText(const Protocol::Message& msg) {
    if (msg.to == "ALL") {
        std::string senderRoom;

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);

            auto it = clients_.find(msg.from);
            if (it == clients_.end()) {
                return;
            }

            senderRoom = it->second.room;
        }

        Protocol::Message roomMsg = msg;
        roomMsg.room = senderRoom;

        broadcastToRoom(senderRoom, roomMsg);
    } else {
        std::shared_ptr<SimpleNet::Socket> senderSocket;
        std::string senderRoom;
        std::string receiverRoom;

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);

            auto senderIt = clients_.find(msg.from);
            if (senderIt == clients_.end()) {
                return;
            }

            senderSocket = senderIt->second.socket;
            senderRoom = senderIt->second.room;

            auto receiverIt = clients_.find(msg.to);
            if (receiverIt == clients_.end()) {
                Protocol::sendMessage(
                    *senderSocket,
                    Protocol::makeError("User '" + msg.to + "' not found")
                );
                return;
            }

            receiverRoom = receiverIt->second.room;
        }

        if (senderRoom != receiverRoom) {
            Protocol::sendMessage(
                *senderSocket,
                Protocol::makeError("User '" + msg.to + "' is not in your room")
            );
            return;
        }

        Protocol::Message roomMsg = msg;
        roomMsg.room = senderRoom;

        bool found = sendTo(msg.to, roomMsg);

        if (found) {
            Protocol::sendMessage(*senderSocket, roomMsg);
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

void ChatServer::broadcastToRoom(const std::string& room,
                                 const Protocol::Message& msg) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    for (auto& [name, info] : clients_) {
        if (info.room != room) {
            continue;
        }
        
        Protocol::sendMessage(*info.socket, msg);
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
 
