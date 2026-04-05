#include "Client.hpp"
#include <iostream>
#include <thread>
#include <sstream>
 
namespace Chat {

ChatClient::ChatClient(const std::string& host, int port, const std::string& username)
    : host_(host)
    , port_(port)
    , username_(username)
    , running_(true)
{}

void ChatClient::run() {
    socket_.connect(host_, port_);
    std::cout << "Connected to " << host_ << ":" << port_ << "\n";

    std::string password;
    std::cout << "Enter password: ";
    std::getline(std::cin, password);

    Protocol::sendMessage(socket_, Protocol::makeAuth(username_, password));

    std::thread recv_thread([this]() {
        receiveLoop();
    });
    recv_thread.detach();

    sendLoop();
}
 
//  receiveLoop() — поток приёма сообщений
void ChatClient::receiveLoop() {
    while (running_) {
        Protocol::Message msg;
 
        if (!Protocol::recvMessage(socket_, msg)) {
            std::cout << "\n[Disconnected from server]\n";
            running_ = false;
            break;
        }
 
        printMessage(msg);
    }
}
 
//  sendLoop() — чтение ввода и отправка сообщений
void ChatClient::sendLoop() {
    std::cout << "Commands:\n";
    std::cout << "  /all <text>        - send to everyone\n";
    std::cout << "  /dm <name> <text>  - send private message\n";
    std::cout << "  /quit              - exit\n\n";
 
    std::string input;
    while (running_) {
        std::getline(std::cin, input);
 
        if (input.empty()) continue;
 
        if (input == "/quit") {
            running_ = false;
            break;
        }
 
        Protocol::Message msg;
        if (parseInput(input, msg)) {
            try {
                Protocol::sendMessage(socket_, msg);
            } catch (...) {
                std::cout << "[Error] Failed to send message\n";
                running_ = false;
                break;
            }
        }
    }
}

//  parseInput() — парсинг команды пользователя
bool ChatClient::parseInput(const std::string& input, Protocol::Message& out) {
    // istringstream — удобный способ разбить строку на слова
    std::istringstream iss(input);
    std::string command;
    iss >> command; 
 
    if (command == "/all") {
        std::string text;
        std::getline(iss, text);
 
        if (!text.empty() && text[0] == ' ') text = text.substr(1);
 
        if (text.empty()) {
            std::cout << "[Error] Usage: /all <text>\n";
            return false;
        }
 
        out = Protocol::makeText(username_, "ALL", text);
        return true;
 
    } else if (command == "/dm") {
        std::string to;
        iss >> to;
 
        if (to.empty()) {
            std::cout << "[Error] Usage: /dm <name> <text>\n";
            return false;
        }
 
        std::string text;
        std::getline(iss, text);
        if (!text.empty() && text[0] == ' ') text = text.substr(1);
 
        if (text.empty()) {
            std::cout << "[Error] Usage: /dm <name> <text>\n";
            return false;
        }
 
        out = Protocol::makeText(username_, to, text);
        return true;
 
    } else {
        std::cout << "[Error] Unknown command. Use /all or /dm\n";
        return false;
    }
}
 
//  printMessage() — вывод сообщения на экран
void ChatClient::printMessage(const Protocol::Message& msg) {
    switch (msg.type) {
        case Protocol::MessageType::SYSTEM:
            std::cout << "[System] " << msg.body << "\n";
            break;
 
        case Protocol::MessageType::ERROR:
            std::cout << "[Error] " << msg.body << "\n";
            break;
 
        case Protocol::MessageType::TEXT:
            if (msg.to == "ALL") {
                // Групповое сообщение
                std::cout << "[" << msg.from << "] " << msg.body << "\n";
            } else {
                // Личное сообщение
                std::cout << "[DM] " << msg.from << " → " << msg.to
                          << ": " << msg.body << "\n";
            }
            break;
 
        default:
            break;
    }
}
 
} // namespace Chat
 