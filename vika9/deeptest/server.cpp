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

    // Track outgoing peer connections (so we can manage them)
    std::map<int, std::thread> peer_threads;
    std::mutex peer_threads_mutex;

    // Message storage (local) - store sender info with each message
    struct Message {
        std::string from_group;
        std::string content;
    };
    std::deque<Message> messages; // FIFO queue: front() oldest, pop_front() on GETMSG
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
            // Server-to-server: HELO,<FROM_GROUP_ID>
            peerName = firstMessage.substr(5);
            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                connections[sockfd] = "peer:" + peerName;  // Mark as peer
            }
            isPeer = true;
            log("Registered peer server: " + peerName + " on fd=" + std::to_string(sockfd));

            // Respond with HELO back to identify ourselves
            std::string helo_reply = "HELO," + group_id;
            Networking::sendMessage(sockfd, helo_reply);
            log("Sent HELO response to peer " + peerName + ": " + helo_reply);

            // Also send SERVERS format: SERVERS,<group>,<ip>,<port>;<group>,<ip>,<port>;...
            sockaddr_in local;
            socklen_t llen = sizeof(local);
            if (getsockname(sockfd, (sockaddr*)&local, &llen) == 0) {
                char buf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
                std::string host_ip = std::string(buf);
                std::string servers_reply = "SERVERS," + group_id + "," + host_ip + "," + std::to_string(server_port) + ";";
                Networking::sendMessage(sockfd, servers_reply);
                log("Sent SERVERS response to peer " + peerName + ": " + servers_reply);
            }
        } else {
            // Client connection (first message is a client command)
            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                connections[sockfd] = "client:unknown";
            }
            log("Connection identified as client on fd=" + std::to_string(sockfd));
            std::string resp = processClientCommand(firstMessage);
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

            // Route to appropriate handler
            if (isPeer) {
                std::string response = processServerCommand(command, sockfd);
                if (!response.empty()) {
                    Networking::sendMessage(sockfd, response);
                    log("Sent reply to peer: " + response);
                }
            } else {
                std::string response = processClientCommand(command);
                if (!response.empty()) {
                    Networking::sendMessage(sockfd, response);
                    log("Sent reply to client: " + response);
                }
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
    
    // Handle CLIENT commands: GETMSG, SENDMSG,GROUP_ID,message, LISTSERVERS
    std::string processClientCommand(const std::string& command) {
        if (command == "GETMSG") {
            std::lock_guard<std::mutex> lock(messages_mutex);
            if (messages.empty()) {
                return "NO_MESSAGES";
            }
            // FIFO: return oldest message and remove it from the queue
            Message msg = messages.front();
            messages.pop_front();
            return "MESSAGE: From: " + msg.from_group + " Msg: " + msg.content;
        }
        else if (command.find("SENDMSG,") == 0) {
            // Client format: SENDMSG,GROUP_ID,<message>
            size_t first_comma = command.find(',');
            size_t second_comma = command.find(',', first_comma + 1);
            
            if (second_comma != std::string::npos) {
                std::string to_group = command.substr(first_comma + 1, second_comma - first_comma - 1);
                std::string message = command.substr(second_comma + 1);

                // Store locally if addressed to this server's group
                if (to_group == group_id) {
                    std::lock_guard<std::mutex> lock(messages_mutex);
                    messages.push_back({group_id, message});
                    log("Stored message from client for group " + to_group + ": " + message);
                }

                // Forward to connected peers in SERVER format: SENDMSG,TO_GROUP,FROM_GROUP,message
                std::string peer_msg = "SENDMSG," + to_group + "," + group_id + "," + message;
                {
                    std::lock_guard<std::mutex> lock(connections_mutex);
                    for (const auto& conn : connections) {
                        if (conn.second.rfind("peer:", 0) == 0) {
                            Networking::sendMessage(conn.first, peer_msg);
                            log("Forwarded to peer: " + peer_msg);
                        }
                    }
                }
                
                return "MESSAGE_SENT";
            }
            return "ERROR,INVALID_FORMAT";
        }
        else if (command == "LISTSERVERS") {
            std::lock_guard<std::mutex> lock(connections_mutex);
            std::string list = "SERVERS";
            for (const auto& conn : connections) {
                if (conn.second.rfind("peer:", 0) == 0) {
                    list += "," + conn.second.substr(5); // Skip "peer:" prefix
                }
            }
            return list;
        }
        else if (command.rfind("CONNECT ", 0) == 0) {
            // Client format: CONNECT <ip> <port>
            // Extract IP and port
            size_t space_pos = command.find(' ', 8); // Find space after "CONNECT "
            if (space_pos == std::string::npos) {
                return "ERROR,INVALID_FORMAT (usage: CONNECT <ip> <port>)";
            }
            
            std::string ip = command.substr(8, space_pos - 8);
            std::string port_str = command.substr(space_pos + 1);
            
            try {
                int port = std::stoi(port_str);
                
                // Initiate connection to peer in a new thread
                std::thread t(&TSAMServer::connectToPeer, this, ip, port);
                t.detach();
                
                return "CONNECTING to " + ip + ":" + port_str;
            } catch (...) {
                return "ERROR,INVALID_PORT";
            }
        }
        return "ERROR,UNKNOWN_COMMAND";
    }

    // Handle SERVER-to-SERVER commands: HELO, KEEPALIVE, GETMSGS, SENDMSG (3-param), STATUSREQ, STATUSRESP
    std::string processServerCommand(const std::string& command, int sockfd = -1) {
        if (command.rfind("HELO,", 0) == 0) {
            // Should have been handled in initial handshake, but handle here too
            std::string from_group = command.substr(5);
            log("Received HELO from " + from_group);
            
            // Respond with HELO back to identify ourselves
            std::string helo_reply = "HELO," + group_id;
            if (sockfd >= 0) {
                Networking::sendMessage(sockfd, helo_reply);
                log("Sent HELO response: " + helo_reply);
                
                // Also send SERVERS list
                std::string servers_reply = "SERVERS," + group_id + "," + "0.0.0.0" + "," + std::to_string(server_port) + ";";
                Networking::sendMessage(sockfd, servers_reply);
                log("Sent SERVERS response: " + servers_reply);
                return ""; // Already sent, no need to return
            }
            return helo_reply;
        }
        else if (command.rfind("SENDMSG,", 0) == 0) {
            // Server format: SENDMSG,TO_GROUP,FROM_GROUP,message
            size_t start = 8; // after "SENDMSG,"
            size_t p1 = command.find(',', start);
            if (p1 == std::string::npos) return "";
            size_t p2 = command.find(',', p1 + 1);
            if (p2 == std::string::npos) return "";

            std::string to_group = command.substr(start, p1 - start);
            std::string from_group = command.substr(p1 + 1, p2 - p1 - 1);
            std::string message = command.substr(p2 + 1);

            log("Server SENDMSG: to=" + to_group + " from=" + from_group + " msg=" + message);

            if (to_group == group_id) {
                std::lock_guard<std::mutex> lock(messages_mutex);
                messages.push_back({from_group, message});
                log("Stored message from peer " + from_group);
            } else {
                // Forward to other peers
                std::lock_guard<std::mutex> lock(connections_mutex);
                for (const auto& conn : connections) {
                    if (conn.second.rfind("peer:", 0) == 0 && conn.second != "peer:" + from_group) {
                        Networking::sendMessage(conn.first, command);
                        log("Forwarded SENDMSG to peer: " + conn.second.substr(5));
                    }
                }
            }
            return ""; // No reply to server SENDMSG
        }
        else if (command.rfind("KEEPALIVE,", 0) == 0) {
            std::string msg_count = command.substr(10);
            log("Received KEEPALIVE with " + msg_count + " messages");
            return ""; // No reply needed
        }
        else if (command.rfind("GETMSGS,", 0) == 0) {
            std::string target_group = command.substr(8);
            log("Received GETMSGS for group: " + target_group);
            // For now, simple implementation: return messages if target is this group
            if (target_group == group_id) {
                std::lock_guard<std::mutex> lock(messages_mutex);
                if (messages.empty()) {
                    return "NO_MESSAGES";
                }
                Message msg = messages.front();
                messages.pop_front();
                return "MESSAGE: From: " + msg.from_group + " Msg: " + msg.content;
            }
            return "NO_MESSAGES";
        }
        else if (command == "STATUSREQ") {
            // Reply with STATUSRESP,<group>,<msg_count>,...
            std::lock_guard<std::mutex> lock(messages_mutex);
            return "STATUSRESP," + group_id + "," + std::to_string(messages.size());
        }
        else if (command.rfind("STATUSRESP,", 0) == 0) {
            log("Received STATUSRESP: " + command);
            return ""; // No reply needed
        }
        else if (command.rfind("SERVERS,", 0) == 0) {
            log("Received SERVERS: " + command);
            return ""; // No reply needed
        }
        else if (command.rfind("ERROR,", 0) == 0) {
            log("Received ERROR from peer: " + command);
            return ""; // No reply needed
        }
        
        log("Unknown server command: " + command);
        return "";
    }

    // Connect to another peer server (initiated by client CONNECT command)
    void connectToPeer(const std::string& ip, int port) {
        log("Attempting to connect to peer " + ip + ":" + std::to_string(port));
        
        try {
            // Create client socket to peer
            int peer_sock = Networking::createClientSocket(ip, port);
            log("Connected to peer " + ip + ":" + std::to_string(port));
            
            // Send HELO as first message
            std::string helo = "HELO," + group_id;
            Networking::sendMessage(peer_sock, helo);
            log("Sent to peer: " + helo);
            
            // Receive responses in a loop
            while (running) {
                std::string response = Networking::receiveMessage(peer_sock);
                if (response.empty()) {
                    log("Peer " + ip + ":" + std::to_string(port) + " disconnected");
                    break;
                }
                
                log("Received from peer " + ip + ":" + std::to_string(port) + ": " + response);
                
                // Handle different responses
                if (response.rfind("SERVERS,", 0) == 0) {
                    // Got SERVERS list, just log it
                    log("Peer sent SERVERS list: " + response);
                }
                // else if (response.rfind("HELO,", 0) == 0) {
                //     // Peer sent HELO to us, respond with SERVERS
                //     std::string peer_group = response.substr(5);
                //     log("Peer sent HELO from group: " + peer_group);
                    
                //     // Register as peer
                //     {
                //         std::lock_guard<std::mutex> lock(connections_mutex);
                //         connections[peer_sock] = "peer:" + peer_group;
                //     }
                    
                //     // Respond with SERVERS
                //     sockaddr_in local;
                //     socklen_t llen = sizeof(local);
                //     if (getsockname(peer_sock, (sockaddr*)&local, &llen) == 0) {
                //         char buf[INET_ADDRSTRLEN];
                //         inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
                //         std::string host_ip = std::string(buf);
                //         std::string reply = "SERVERS," + group_id + "," + host_ip + "," + std::to_string(server_port) + ";";
                //         Networking::sendMessage(peer_sock, reply);
                //         log("Sent SERVERS response to peer: " + reply);
                //     }
                // }
                else if (response.rfind("SENDMSG,", 0) == 0) {
                    // Handle server-to-server SENDMSG
                    std::string reply = processServerCommand(response, peer_sock);
                    if (!reply.empty()) {
                        Networking::sendMessage(peer_sock, reply);
                        log("Sent reply to peer: " + reply);
                    }
                }
                else {
                    // Other server commands
                    std::string reply = processServerCommand(response, peer_sock);
                    if (!reply.empty()) {
                        Networking::sendMessage(peer_sock, reply);
                        log("Sent reply to peer: " + reply);
                    }
                }
            }
            
            // Cleanup
            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                connections.erase(peer_sock);
            }
            close(peer_sock);
            log("Closed connection to peer " + ip + ":" + std::to_string(port));
            
        } catch (const std::exception& e) {
            log("Failed to connect to peer " + ip + ":" + std::to_string(port) + " - " + std::string(e.what()));
        }
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