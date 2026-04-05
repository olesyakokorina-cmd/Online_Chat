#include "Protocol.hpp"
#include <stdexcept>
#include <arpa/inet.h>  // htonl(), ntohl()
 
namespace Protocol {

static std::string encodeField(const std::string& value) {
    return std::to_string(value.size()) + "#" + value;
}
 
static std::string decodeField(const std::string& raw, size_t& pos) {
    size_t hash = raw.find('#', pos);
    if (hash == std::string::npos) {
        throw std::runtime_error("Protocol: malformed message");
    }

    size_t len = std::stoul(raw.substr(pos, hash - pos));
    std::string value = raw.substr(hash + 1, len);
    pos = hash + 1 + len;
 
    return value;
}
 
// ── typeToString / stringToType ──
 
static std::string typeToString(MessageType type) {
    switch (type) {
        case MessageType::AUTH:   return "AUTH";
        case MessageType::TEXT:   return "TEXT";
        case MessageType::SYSTEM: return "SYSTEM";
        case MessageType::ERROR:  return "ERROR";
        default: throw std::runtime_error("Protocol: unknown type");
    }
}
 
static MessageType stringToType(const std::string& s) {
    if (s == "AUTH")   return MessageType::AUTH;
    if (s == "TEXT")   return MessageType::TEXT;
    if (s == "SYSTEM") return MessageType::SYSTEM;
    if (s == "ERROR")  return MessageType::ERROR;
    throw std::runtime_error("Protocol: unknown type string: " + s);
}
 
// ── serialize / deserialize ──
 
std::string serialize(const Message& msg) {
    std::string result;
    result += encodeField(typeToString(msg.type));
    result += encodeField(msg.from);
    result += encodeField(msg.to);
    result += encodeField(msg.body);
    return result;
}
 
Message deserialize(const std::string& raw) {
    size_t pos = 0;
    Message msg;
    msg.type = stringToType(decodeField(raw, pos));
    msg.from = decodeField(raw, pos);
    msg.to   = decodeField(raw, pos);
    msg.body = decodeField(raw, pos);
    return msg;
}

static void sendAll(SimpleNet::Socket& sock, const char* data, size_t count) {
    size_t sent = 0;
    while (sent < count) {
        ssize_t n = sock.send(std::string_view(data + sent, count - sent));
        if (n <= 0) throw std::runtime_error("Connection closed");
        sent += n;
    }
}

static bool recvAll(SimpleNet::Socket& sock, char* buf, size_t count) {
    size_t got = 0;
    while (got < count) {
        auto chunk = sock.receive(count - got);
        if (chunk.empty()) return false;
        std::copy(chunk.begin(), chunk.end(), buf + got);
        got += chunk.size();
    }
    return true;
}
 
void sendMessage(SimpleNet::Socket& sock, const Message& msg) {
    std::string data = serialize(msg);

    uint32_t net_len = htonl(data.size());
    sendAll(sock, reinterpret_cast<const char*>(&net_len), 4);
 
    sendAll(sock, data.data(), data.size());
}
 
bool recvMessage(SimpleNet::Socket& sock, Message& out) {
    uint32_t net_len = 0;
    if (!recvAll(sock, reinterpret_cast<char*>(&net_len), 4)) {
        return false;
    }
 
    uint32_t len = ntohl(net_len);

    if (len > 1024 * 1024) {
        throw std::runtime_error("Protocol: message too large");
    }
 
    std::string data(len, '\0');
    if (!recvAll(sock, data.data(), len)) {
        return false;
    }
 
    out = deserialize(data);
    return true;
}
 
Message makeAuth(const std::string& username, const std::string& password) {
    return { MessageType::AUTH, username, "", password };
}
 
Message makeText(const std::string& from, const std::string& to, const std::string& body) {
    return { MessageType::TEXT, from, to, body };
}
 
Message makeSystem(const std::string& body) {
    return { MessageType::SYSTEM, "SERVER", "ALL", body };
}
 
Message makeError(const std::string& reason) {
    return { MessageType::ERROR, "SERVER", "", reason };
}
 
} // namespace Protocol
 