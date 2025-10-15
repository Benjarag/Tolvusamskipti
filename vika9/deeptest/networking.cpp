#include "networking.h"
#include "protocol.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <system_error>
#include <cerrno>
#include <iostream>
#include <iomanip>

bool Networking::sendMessage(int socket, const std::string& command) {
    auto message = Protocol::encodeMessage(command);
    
    ssize_t bytesSent = send(socket, message.data(), message.size(), 0);
    return bytesSent == static_cast<ssize_t>(message.size());
}

// Helper: read exactly n bytes or return -1 on error/EOF
// Returns:
//  n  : successfully read exactly n bytes
//  -2 : EOF (peer closed connection)
//  -1 : error (errno set by recv)
static ssize_t read_exact(int sock, void* buf, size_t n) {
    // Cast to uint8_t* for byte-wise pointer arithmetic
    uint8_t* dst = static_cast<uint8_t*>(buf);
    size_t remaining = n;
    // Keep reading until we've read exactly n bytes
    while (remaining > 0) {
        ssize_t nread = recv(sock, dst, remaining, 0);
        if (nread == 0) {
            // orderly shutdown by peer
            return -2;
        }
        if (nread < 0) {
            // recv set errno
            return -1;
        }
        dst += nread;
        remaining -= nread;
    }
    // Successfully read n bytesS
    return static_cast<ssize_t>(n);
}

std::string Networking::receiveMessage(int socket) {
    uint8_t header[3];
    ssize_t hres = read_exact(socket, header, 3);
    if (hres != 3) {
        if (hres == -2) {
            std::cerr << "[networking] read_exact(header) EOF (peer closed)\n";
        } else if (hres == -1) {
            int err = errno;
            std::cerr << "[networking] read_exact(header) failed: " << std::strerror(err) << "\n";
        } else {
            std::cerr << "[networking] read_exact(header) unexpected return=" << hres << "\n";
        }
        return ""; // incomplete
    }

    if (header[0] != SOH) {
        // bad framing: log the first few bytes to help debugging and return
        std::cerr << "[networking] invalid SOH: got 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(header[0])
                  << ", header bytes: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(header[0])
                  << " 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(header[1])
                  << " 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(header[2])
                  << std::dec << "\n";
        return "";
    }

    uint16_t totalLength = Protocol::getMessageLength(std::vector<uint8_t>(header, header+3));
    // Sanity checks: minimal frame is 5 bytes (SOH + len(2) + STX + ETX)
    if (totalLength < 5 || totalLength > /*MAX*/5120) {
        std::cerr << "[networking] invalid totalLength=" << totalLength << "\n";
        return "";
    }

    // We already read 3 bytes; read remaining (totalLength - 3)
    std::vector<uint8_t> buffer(totalLength);
    // copy header we already read
    memcpy(buffer.data(), header, 3);
    ssize_t bres = read_exact(socket, buffer.data() + 3, totalLength - 3);
    if (bres != static_cast<ssize_t>(totalLength - 3)) {
        if (bres == -2) {
            std::cerr << "[networking] read_exact(body) EOF (peer closed)\n";
        } else if (bres == -1) {
            int err = errno;
            std::cerr << "[networking] read_exact(body) failed: " << std::strerror(err) << "\n";
        } else {
            std::cerr << "[networking] read_exact(body) unexpected return=" << bres << "\n";
        }
        return "";
    }

    try {
        return Protocol::decodeMessage(buffer);
    } catch (...) {
        std::cerr << "[networking] Protocol::decodeMessage threw (invalid framing)\n";
        return "";
    }
}

int Networking::createServerSocket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        throw std::system_error(errno, std::system_category(), "socket creation failed");
    }
    
    // Set SO_REUSEADDR
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "bind failed");
    }
    
    // Increase backlog slightly to make it easier to accept bursts of incoming
    // peer connections during discovery.
    if (listen(server_fd, 50) < 0) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "listen failed");
    }
    
    return server_fd;
}

int Networking::createClientSocket(const std::string& host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0); // AF_INET for IPv4, SOCK_STREAM for TCP, 0 for default protocol
    if (sock == -1) {
        throw std::system_error(errno, std::system_category(), "socket creation failed");
    }

    // Set up the server address structure
    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    // Convert IP addresses from text to binary form
    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
        close(sock);
        throw std::runtime_error("Invalid address");
    }

    // Connect to the server
    if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        throw std::system_error(errno, std::system_category(), "connect failed");
    }


    return sock;
}