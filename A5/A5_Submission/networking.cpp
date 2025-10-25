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
#include <cstdlib>
#include <netdb.h>

std::string Networking::advertised_ip() {
    // 1. Environment variable override (for external/port-forwarded IPs)
    const char* env = std::getenv("TSAM_PUBLIC_IP");
    if (env && *env) {
        return std::string(env);
    }

    // 2. Try to resolve the hostname to a local IPv4 address
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct addrinfo hints{}, *info;
        hints.ai_family = AF_INET; // IPv4 only
        if (getaddrinfo(hostname, nullptr, &hints, &info) == 0) {
            char ip[INET_ADDRSTRLEN];
            struct sockaddr_in* addr = (struct sockaddr_in*)info->ai_addr;
            inet_ntop(AF_INET, &(addr->sin_addr), ip, sizeof(ip));
            freeaddrinfo(info);
            
            // Skip loopback/localhost addresses
            std::string result(ip);
            // Avoid advertising loopback addresses
            if (result != "127.0.0.1" && result != "127.0.1.1") {
                // Reject private/container IPs (RFC1918) - prefer caller fallback or TSAM_PUBLIC_IP
                auto isPrivate = [](const std::string &s) {
                    // quick checks for 10., 172.16-31., 192.168.
                    if (s.rfind("10.", 0) == 0) return true;
                    if (s.rfind("192.168.", 0) == 0) return true;
                    if (s.rfind("172.", 0) == 0) {
                        // parse second octet
                        size_t p1 = s.find('.', 4);
                        if (p1 != std::string::npos) {
                            std::string sec = s.substr(4, p1 - 4);
                            try {
                                int v = std::stoi(sec);
                                if (v >= 16 && v <= 31) return true;
                            } catch(...) {} // do nothing on error
                        }
                    }
                    return false;
                };

                if (!isPrivate(result)) {
                    return result;
                }
                // else fall through to next detection method (don't return private addresses)
            }
        }
    }

    // 3. Fallback: try to determine the outgoing interface IP
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("8.8.8.8"); // dummy external target
        addr.sin_port = htons(53); // DNS port

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            struct sockaddr_in local_addr;
            socklen_t len = sizeof(local_addr);
            if (getsockname(sock, (struct sockaddr*)&local_addr, &len) == 0) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &local_addr.sin_addr, ip, sizeof(ip));
                close(sock);
                
                // Skip loopback and private/container IPs
                std::string result(ip);
                auto isPrivate = [](const std::string &s) {
                    if (s.rfind("10.", 0) == 0) return true;
                    if (s.rfind("192.168.", 0) == 0) return true;
                    if (s.rfind("172.", 0) == 0) {
                        size_t p1 = s.find('.', 4);
                        if (p1 != std::string::npos) {
                            std::string sec = s.substr(4, p1 - 4);
                            try {
                                int v = std::stoi(sec);
                                if (v >= 16 && v <= 31) return true;
                            } catch(...) {}
                        }
                    }
                    return false;
                };

                if (result != "127.0.0.1" && result != "127.0.1.1" && !isPrivate(result)) {
                    return result;
                }
            }
        }
        close(sock);
    }

    // 4. Final fallback: return a sensible public default so SERVERS replies aren't advertising
    // internal/container IPs in common lab setups. Prefer explicit TSAM_PUBLIC_IP if you need
    // a different address. This default matches the TSAM public address used in logs/README.
    std::cerr << "[networking] advertised_ip(): no public IP detected, using default public IP 130.208.246.98\n";
    return std::string("130.208.246.98");
}

bool Networking::sendMessage(int socket, const std::string& command) {
    std::string cmd = command;
    if (cmd.size() > PROTOCOL_MAX_PAYLOAD) {
        cmd = cmd.substr(0, PROTOCOL_MAX_PAYLOAD);
        std::cerr << "[networking] sendMessage: command truncated to " << std::to_string(PROTOCOL_MAX_PAYLOAD) << " bytes\n";
    }
    auto message = Protocol::encodeMessage(cmd);
    
    ssize_t sent = 0;
    while (sent < static_cast<ssize_t>(message.size())) {
        ssize_t n = ::send(socket, message.data() + sent, message.size() - sent, 0);
        if (n >= 0) {
            sent += n;
        } else {
            if (errno == EINTR) {
                continue;  // Interrupted, try again
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block - message partially sent
                // In production, you'd want to buffer remainder, but for now return false
                return false;
            } else {
                // Real error
                return false;
            }
        }
    }
    return true;
}

// Helper: read exactly n bytes or return -1 on error/EOF
// Returns:
//  n  : successfully read exactly n bytes
//  -2 : EOF (peer closed connection)
//  -1 : error (errno set by recv)
//  -3 : EAGAIN/EWOULDBLOCK (would block on non-blocking socket)
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
            // Check if it's EAGAIN/EWOULDBLOCK (would block)
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -3;
            }
            // recv set errno to real error
            return -1;
        }
        dst += nread;
        remaining -= nread;
    }
    // Successfully read n bytes
    return static_cast<ssize_t>(n);
}

std::string Networking::receiveMessage(int socket) {
    uint8_t header[3];
    ssize_t hres = read_exact(socket, header, 3);
    if (hres != 3) {
        if (hres == -3) {
            // Would block - no data available yet
            return "";
        }
        if (hres == -2) {
            // EOF
            return "";
        } else if (hres == -1) {
            int err = errno;
            std::cerr << "[networking] read_exact(header) failed: " << std::strerror(err) << "\n";
        } else {
            std::cerr << "[networking] read_exact(header) unexpected return=" << hres << "\n";
        }
        return ""; // incomplete
    }

    if (header[0] != SOH) {
        // bad framing: socket stream is corrupted
        std::cerr << "[networking] invalid SOH: got 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(header[0])
                  << ", header bytes: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(header[0])
                  << " 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(header[1])
                  << " 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(header[2])
                  << std::dec << "\n";
        // Signal corruption with special prefix - server will close this connection
        return "__FRAME_CORRUPTION__";
    }

    uint16_t totalLength = Protocol::getMessageLength(std::vector<uint8_t>(header, header+3));
    // Sanity checks: minimal frame is 5 bytes (SOH + len(2) + STX + ETX)
    if (totalLength < PROTOCOL_MIN_FRAME || totalLength > PROTOCOL_MAX_FRAME) {
        std::cerr << "[networking] invalid totalLength=" << totalLength << "\n";
        return "";
    }

    // We already read 3 bytes; read remaining (totalLength - 3)
    std::vector<uint8_t> buffer(totalLength);
    // copy header we already read
    memcpy(buffer.data(), header, 3);
    ssize_t bres = read_exact(socket, buffer.data() + 3, totalLength - 3);
    if (bres != static_cast<ssize_t>(totalLength - 3)) {
        if (bres == -3) {
            // Would block
            return "";
        }
        if (bres == -2) {
            // EOF
            return "";
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