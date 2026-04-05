#pragma once
 
#include "Socket.hpp"
#include <string>
#include <cstdint>
 
namespace Protocol {

enum class MessageType {
    AUTH,
    TEXT,
    SYSTEM,
    ERROR
};
 
struct Message {
    MessageType type;
    std::string from;
    std::string to;
    std::string body;
};

std::string serialize(const Message& msg);
 

Message deserialize(const std::string& raw);

void sendMessage(SimpleNet::Socket& sock, const Message& msg);

bool recvMessage(SimpleNet::Socket& sock, Message& out);
 
Message makeAuth(const std::string& username);
Message makeText(const std::string& from, const std::string& to, const std::string& body);
Message makeSystem(const std::string& body);
Message makeError(const std::string& reason);
 
} // namespace Protocol