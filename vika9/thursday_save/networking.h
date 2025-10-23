#ifndef NETWORKING_H
#define NETWORKING_H

#include <vector>
#include <string>
#include <cstdint>
#include "protocol.h"

class Networking {
public:
    static bool sendMessage(int socket, const std::string& command);
    static std::string receiveMessage(int socket);
    static int createServerSocket(int port);
    static int createClientSocket(const std::string& host, int port);
    
private:
    // Buffer must be able to hold the full framed message. The assignment
    // specifies 5000 bytes of payload and 5 bytes of framing -> 5005 bytes.
    static const int BUFFER_SIZE = static_cast<int>(PROTOCOL_MAX_FRAME);
};

#endif