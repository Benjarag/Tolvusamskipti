#ifndef NETWORKING_H
#define NETWORKING_H

#include <vector>
#include <string>
#include <cstdint>

class Networking {
public:
    static bool sendMessage(int socket, const std::string& command);
    static std::string receiveMessage(int socket);
    static int createServerSocket(int port);
    static int createClientSocket(const std::string& host, int port);
    
private:
    static const int BUFFER_SIZE = 5120; // 5KB as per specification
};

#endif