// server.cpp
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "networking.h"
#include "protocol.h"
// FIFO message queue
#include <deque>

class TSAMServer {
private:
    std::atomic<bool> running;
    int server_port;
    std::string group_id;

    // Use ONE map for all connections (simpler)
    std::map<int, std::string> connections;  // socket_fd -> "type:name"
    std::mutex connections_mutex;

    // Message storage (local)
    std::deque<std::string> messages; // FIFO queue: front() oldest, pop_front() on GETMSG
    std::mutex messages_mutex;
    
    void log(const std::string& message) {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::cout << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << message << std::endl;
    }
    
    // Per-connection handler: classifies the socket (peer vs client) based on
    // the first framed message, then loops reading messages.
    void handleConnection(int sockfd) {
        log("Handler thread started for fd=" + std::to_string(sockfd));

        bool isPeer = false;
        std::string peerName;

        // Read first framed message to classify the connection
        std::string firstMessage = Networking::receiveMessage(sockfd);
        if (firstMessage.empty()) {
            log("Connection closed before any data from fd=" + std::to_string(sockfd));
            close(sockfd);
            return;
        }

        // Determine if it's a server peer HELO or a client command
        if (firstMessage.rfind("HELO,", 0) == 0) {
            peerName = firstMessage.substr(5);
            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                connections[sockfd] = "peer:" + peerName;  // Mark as peer
            }
            isPeer = true;
            log("Registered peer server: " + peerName + " on fd=" + std::to_string(sockfd));

            // Respond with SERVERS
            sockaddr_in local;
            socklen_t llen = sizeof(local);
            // Use getsockname() to get OUR local address information for this socket
            // This tells us which IP and port the peer connected TO (our server's address)
            if (getsockname(sockfd, (sockaddr*)&local, &llen) == 0) {
                char buf[INET_ADDRSTRLEN];
                // Convert the binary IP address to human-readable string
                inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
                std::string host_ip = std::string(buf); // This is OUR REAL IP address, NOT 127.0.0.1!
                std::string reply = "SERVERS," + group_id + "," + host_ip + "," + std::to_string(server_port) + ";";
                Networking::sendMessage(sockfd, reply);
            }
        } else {
            // Client connection
            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                connections[sockfd] = "client:unknown";  // Mark as client
            }
            log("Connection identified as client on fd=" + std::to_string(sockfd));
            std::string resp = processCommand(firstMessage);
            if (!resp.empty()) {
                Networking::sendMessage(sockfd, resp);
                log("Sent to client: " + resp);
            }
        }

        // Main loop: receive and process further messages on this connection
        while (running) {
            std::string command = Networking::receiveMessage(sockfd);
            if (command.empty()) break;

            log("Received from connection " + std::string(isPeer ? ("peer:" + peerName) : "client") + " " + command);

            // Special handling: if it's a peer SENDMSG, store it locally if for us
            if (isPeer && command.rfind("SENDMSG,", 0) == 0) {
                // Format: SENDMSG,TO_GROUP,FROM_GROUP,message
                // Example: SENDMSG,A5_29,A5_30,hello
                size_t start = 8; // after "SENDMSG,"
                size_t p1 = command.find(',', start);
                if (p1 == std::string::npos) continue;
                size_t p2 = command.find(',', p1 + 1);
                if (p2 == std::string::npos) continue;

                std::string to_group = command.substr(start, p1 - start);
                std::string from_group = command.substr(p1 + 1, p2 - p1 - 1);
                std::string message = command.substr(p2 + 1);

                log("Processing SENDMSG command: " + command);
                log("to_group: " + to_group + ", server group_id: " + group_id);
                if (to_group == group_id) {
                    std::lock_guard<std::mutex> lock(messages_mutex);
                    // Store only the payload so GETMSG returns the original message content in FIFO order
                    messages.push_back(message);
                    log("Stored message from peer " + from_group + ": " + message);
                } else {
                    log("Message not for this group. Intended for: " + to_group);
                }
                continue; // this means that we do not send a reply back to the peer it hops out of the while loop
            }

            std::string response = processCommand(command);
            if (!response.empty()) {
                Networking::sendMessage(sockfd, response);
                log("Sent reply: " + response);
            }
        }

        // Cleanup
        {
            std::lock_guard<std::mutex> lock(connections_mutex);
            connections.erase(sockfd);
        }

        close(sockfd);
        log("Connection disconnected fd=" + std::to_string(sockfd));
    }
    
    std::string processCommand(const std::string& command) {
        if (command == "GETMSG") {
            std::lock_guard<std::mutex> lock(messages_mutex);
            if (messages.empty()) {
                return "NO_MESSAGES";
            }
            // FIFO: return oldest message and remove it from the queue
            std::string msg = messages.front();
            messages.pop_front();
            return "MESSAGE: " + msg;
        }
        else if (command.find("SENDMSG,") == 0) {
            // Format: SENDMSG,GROUP_ID,message
            size_t first_comma = command.find(',');
            size_t second_comma = command.find(',', first_comma + 1);
            
            if (second_comma != std::string::npos) {
                std::string to_group = command.substr(first_comma + 1, second_comma - first_comma - 1);
                std::string message = command.substr(second_comma + 1);

                // If the message is addressed to this server's group, store it locally
                if (to_group == group_id) {
                    std::lock_guard<std::mutex> lock(messages_mutex);
                    // Store the payload (FIFO queue)
                    messages.push_back(message);
                    log("Stored message from client for group " + to_group + ": " + message);
                }

                // Forward to connected peers in server-to-server format
                std::string peer_msg = "SENDMSG," + to_group + "," + group_id + "," + message;
                {
                    std::lock_guard<std::mutex> lock(connections_mutex);
                    for (const auto& conn : connections) {
                        if (conn.second.rfind("peer:", 0) == 0) {
                            Networking::sendMessage(conn.first, peer_msg);
                            log("Forwarded message to peer: " + peer_msg);
                        }
                    }
                }
                
                return "MESSAGE_SENT";
            }
            return "ERROR,INVALID_FORMAT";
        }
        else if (command == "LISTSERVERS") {
            // This should list servers that this server is actually connected to, not hardcoded ip addresses
            std::lock_guard<std::mutex> lock(connections_mutex);
            std::string list = "SERVERS";

            // List all connected peer servers with fd and description
            for (const auto& conn : connections) {
                if (conn.second.rfind("peer:", 0) == 0) {
                    list += "," + conn.second.substr(5); // Skip "peer:"
                }
            }
            return list;
        }
        return "ERROR,UNKNOWN_COMMAND";
    }
    
public:
    // Initialize members in the order they are declared to avoid -Wreorder warnings
    TSAMServer(const std::string& id, int port) : running(false), server_port(port), group_id(id) {}
    
    void start() {
        running = true;
        
        try {
            // Create server socket
            int server_fd = Networking::createServerSocket(server_port);
            log("Server " + group_id + " listening on port " + std::to_string(server_port));
            
            // Polling setup
            // Using poll to handle multiple connections
            std::vector<pollfd> fds;
            pollfd server_pollfd;
            server_pollfd.fd = server_fd;
            server_pollfd.events = POLLIN;
            fds.push_back(server_pollfd);
            
            // Main loop: while the server is running, accept and handle connections
            while (running) {
                // Wait for events on the server socket
                int poll_count = poll(fds.data(), fds.size(), 1000); // 1 second timeout
                
                if (poll_count < 0) {
                    if (running) {
                        log("Poll error");
                    }
                    break;
                }
                
                if (poll_count == 0) {
                    continue; // Timeout
                }
                
                // Check for new connections on listening socket and accept them
                if (fds[0].revents & POLLIN) {
                    sockaddr_in connection_addr;
                    socklen_t connection_len = sizeof(connection_addr);
                    int client_socket = accept(server_fd, (sockaddr*)&connection_addr, &connection_len);

                    if (client_socket >= 0) {
                        // Log peer IP:port and store human-readable entry
                        char peer_ipbuf[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &connection_addr.sin_addr, peer_ipbuf, sizeof(peer_ipbuf));
                        int peer_port = ntohs(connection_addr.sin_port);
                        {
                            std::lock_guard<std::mutex> lock(connections_mutex);
                            connections[client_socket] = std::string("client:") + peer_ipbuf + ":" + std::to_string(peer_port);
                        }
                        log(std::string("New connection accepted from ") + peer_ipbuf + ":" + std::to_string(peer_port));

                        // Immediately hand the socket to a handler thread
                        std::thread t(&TSAMServer::handleConnection, this, client_socket);
                        t.detach();
                    }
                }
            }
            
            close(server_fd);
        } catch (const std::exception& e) {
            log("Server error: " + std::string(e.what()));
        }
        
        log("Server stopped");
    }
    
    void stop() {
        running = false;
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }
    
    int port = std::stoi(argv[1]);
    std::string group_id = "A5_29"; // Your group ID
    
    // Ignore SIGPIPE to prevent crashes on broken pipes
    signal(SIGPIPE, SIG_IGN);
    
    TSAMServer server(group_id, port);
    
    std::cout << "Starting TSAM Server " << group_id << " on port " << port << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;
    
    // Handle Ctrl+C
    signal(SIGINT, [](int) { /* Handler in main thread */ });
    
    server.start();
    
    return 0;
}