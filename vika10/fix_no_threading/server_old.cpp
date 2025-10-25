// server.cpp
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <chrono>
#include <fcntl.h>
#include <errno.h>
#include "networking.h"
#include "protocol.h"
// FIFO message queue
#include <deque>

class TSAMServer {
private:
    std::atomic<bool> running;
    int server_port;
    std::string group_id;

    // Connection state for each socket
    struct ConnectionState {
        std::string buffer;
        bool handshake_complete;
        bool is_peer;
        std::string peer_name;
        std::string peer_ip;
        int peer_port;
        
        ConnectionState() : buffer(""), handshake_complete(false), 
                          is_peer(false), peer_name(""), peer_ip(""), peer_port(0) {}
    };
    
    std::map<int, ConnectionState> connection_states;

    // Message storage (local) - store sender info with each message
    struct Message {
        std::string from_group;
        std::string content;
    };
    struct StoredMessage {
        std::string from_group;
        std::string to_group;
        std::string content;
        std::time_t timestamp;
    };
    std::map<std::string, std::deque<StoredMessage>> group_messages; // group_id -> messages    
    
    // File logging
    std::ofstream logfile;
    
    // Pending outgoing connections (non-blocking)
    struct PendingConnection {
        std::string ip;
        int port;
        int sockfd;
        std::chrono::steady_clock::time_point connect_time;
    };
    std::vector<PendingConnection> pending_connections;

    
    void log(const std::string& message) {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::ostringstream oss;
        oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << message;
        std::string line = oss.str();

        // Write to stdout and to logfile (no mutex needed - single threaded)
        std::cout << line << std::endl;
        if (logfile.is_open()) {
            logfile << line << std::endl;
            logfile.flush();
        }
    }
    
    // Process a message from a socket (single-threaded)
    void processMessage(int sockfd, const std::string& message, std::vector<pollfd>& fds) {
        ConnectionState& state = connection_states[sockfd];
        
        if (!state.handshake_complete) {
            // First message - determine if peer or client
            if (message.rfind("HELO,", 0) == 0) {
                // Server-to-server: HELO,<FROM_GROUP_ID>
                std::string peerName = message.substr(5);
                state.is_peer = true;
                state.peer_name = peerName;
                state.handshake_complete = true;
                
                log("Registered peer server: " + peerName + " on fd=" + std::to_string(sockfd));

                // Respond with HELO back
                std::string helo_reply = "HELO," + group_id;
                Networking::sendMessage(sockfd, helo_reply);
                log("Sent HELO response to peer " + peerName + ": " + helo_reply);

                // Send SERVERS list
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
                // Client connection
                state.is_peer = false;
                state.handshake_complete = true;
                log("Connection identified as client on fd=" + std::to_string(sockfd));
                std::string resp = processClientCommand(message);
                if (!resp.empty()) {
                    Networking::sendMessage(sockfd, resp);
                    log("Sent to client: " + resp);
                }
            }
        } else {
            // Subsequent messages
            log("Received from connection " + std::string(state.is_peer ? ("peer:" + state.peer_name) : "client") + " " + message);
            
            if (state.is_peer) {
                std::string response = processServerCommand(message, sockfd, fds);
                if (!response.empty()) {
                    Networking::sendMessage(sockfd, response);
                    log("Sent reply to peer: " + response);
                }
            } else {
                std::string response = processClientCommand(message, fds);
                if (!response.empty()) {
                    Networking::sendMessage(sockfd, response);
                    log("Sent reply to client: " + response);
                }
            }
        }
    }
    
    // Close a connection and remove from poll list
    void closeConnection(int sockfd, std::vector<pollfd>& fds) {
        log("Closing connection fd=" + std::to_string(sockfd));
        close(sockfd);
        connection_states.erase(sockfd);
        
        // Remove from fds vector
        for (size_t i = 0; i < fds.size(); i++) {
            if (fds[i].fd == sockfd) {
                fds.erase(fds.begin() + i);
                break;
            }
        }
    }
    
    // Handle CLIENT commands: GETMSG, SENDMSG,GROUP_ID,message, LISTSERVERS
    std::string processClientCommand(const std::string& command) {
        if (command == "GETMSG") {
            std::lock_guard<std::mutex> lock(messages_mutex);
            auto &q = group_messages[group_id];
            if (q.empty()) {
                return "NO_MESSAGES";
            }
            StoredMessage msg = q.front();
            q.pop_front();
            return "MESSAGE: From: " + msg.from_group + " Msg: " + msg.content;
        }
        else if (command.find("SENDMSG,") == 0) {
            // Client format: SENDMSG,GROUP_ID,<message>
            size_t first_comma = command.find(',');
            size_t second_comma = command.find(',', first_comma + 1);
            
            // if format is valid
            if (second_comma != std::string::npos) {
                std::string to_group = command.substr(first_comma + 1, second_comma - first_comma - 1);
                // The rest is the message
                std::string message = command.substr(second_comma + 1);

                // Store locally if addressed to this server's group
                if (to_group == group_id) {
                    std::lock_guard<std::mutex> lock(messages_mutex);
                    StoredMessage sm;
                    sm.from_group = group_id;
                    sm.to_group = to_group;
                    sm.content = message;
                    sm.timestamp = std::time(nullptr);
                    group_messages[to_group].push_back(sm);
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
            // Lock both maps since we read from both
            std::lock_guard<std::mutex> lock(connections_mutex);
            std::lock_guard<std::mutex> lock2(peer_info_mutex);
            std::string list = "SERVERS,";
            for (const auto& conn : connections) {
                if (conn.second.rfind("peer:", 0) == 0) {
                    std::string peer_name = conn.second.substr(5);
                    if (peer_info.find(conn.first) != peer_info.end()) {
                        auto& info = peer_info[conn.first];
                        list += info.group_id + "," + info.ip + "," + 
                            std::to_string(info.port) + ";";
                    }
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
                // Register this connection as a peer (thread-safe)
                {
                    std::lock_guard<std::mutex> lock(connections_mutex);
                    std::lock_guard<std::mutex> lock2(peer_info_mutex); 
                    
                    connections[sockfd] = "peer:" + from_group;
                    
                    // Update or create peer_info entry
                    if (peer_info.find(sockfd) != peer_info.end()) {
                        // Update existing entry
                        peer_info[sockfd].group_id = from_group;
                    } else {
                        // Create new entry
                        PeerInfo info;
                        info.group_id = from_group;
                        // ip and port will be updated when we receive SERVERS command
                        peer_info[sockfd] = info;
                    }
                }
                Networking::sendMessage(sockfd, helo_reply);
                log("Sent HELO response: " + helo_reply);
                
                // Also send SERVERS list
                std::string servers_reply = "SERVERS," + group_id + "," + "130.208.246.98" + "," + std::to_string(server_port) + ";";
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


            // ALWAYS store the message, regardless of destination
            {
                std::lock_guard<std::mutex> lock(messages_mutex);
                StoredMessage stored_msg;
                stored_msg.from_group = from_group;
                stored_msg.to_group = to_group;
                stored_msg.content = message;
                stored_msg.timestamp = std::time(nullptr);
                group_messages[to_group].push_back(stored_msg);
                log("Stored message for group " + to_group + " from " + from_group);
            }

            // Forward to other peers (except the sender)
            if (to_group != group_id) {
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
                auto &q = group_messages[group_id];
                if (q.empty()) {
                    return "NO_MESSAGES";
                }
                StoredMessage msg = q.front();
                q.pop_front();
                return "MESSAGE: From: " + msg.from_group + " Msg: " + msg.content;
            }
            return "NO_MESSAGES";
        }
        else if (command == "STATUSREQ") {
            // Reply with STATUSRESP,<group>,<msg_count>,...
            {
                std::lock_guard<std::mutex> lock(messages_mutex);
                // compute total stored messages for our group
                int total = 0;
                for (const auto &p : group_messages) total += p.second.size();
                return "STATUSRESP," + group_id + "," + std::to_string(total);
            }
        }
        else if (command.rfind("STATUSRESP,", 0) == 0) {
            log("Received STATUSRESP: " + command);
            return ""; // 
        }
        else if (command.rfind("SERVERS,", 0) == 0) {
            log("Received SERVERS: " + command);

            // Parse and store peer server addresses
            if (sockfd >= 0) {
                std::string servers_list = command.substr(8); // Remove "SERVERS,"

                // Parse ALL server entries: format is group,ip,port;group,ip,port;...
                std::vector<std::tuple<std::string,std::string,int>> parsed_servers;
                size_t start = 0;
                while (start < servers_list.length()) {
                    size_t semicolon = servers_list.find(';', start);
                    if (semicolon == std::string::npos) break;
                    
                    std::string entry = servers_list.substr(start, semicolon - start);
                    size_t comma1 = entry.find(',');
                    size_t comma2 = entry.find(',', (comma1==std::string::npos)?std::string::npos:comma1+1);
                    
                    if (comma1 != std::string::npos && comma2 != std::string::npos) {
                        std::string peer_group = entry.substr(0, comma1);
                        std::string peer_ip = entry.substr(comma1 + 1, comma2 - comma1 - 1);
                        std::string peer_port_str = entry.substr(comma2 + 1);
                        try {
                            int peer_port = std::stoi(peer_port_str);
                            parsed_servers.emplace_back(peer_group, peer_ip, peer_port);
                        } catch (...) {
                            log("Error parsing peer port: " + peer_port_str);
                        }
                    }
                    start = semicolon + 1;
                }

                // Update peer_info for our immediate connection and prepare auto-connect list
                {
                    std::lock_guard<std::mutex> lock(connections_mutex);
                    std::lock_guard<std::mutex> lock2(peer_info_mutex);
                    for (const auto &t : parsed_servers) {
                        const std::string &peer_group = std::get<0>(t);
                        const std::string &peer_ip = std::get<1>(t);
                        int peer_port = std::get<2>(t);
                        // Update peer_info for this sockfd if it's the same group entry
                        if (peer_info.find(sockfd) != peer_info.end()) {
                            // Nothing to match against here other than updating; store last seen
                            peer_info[sockfd].ip = peer_ip;
                            peer_info[sockfd].port = peer_port;
                            log("Updated peer info for fd=" + std::to_string(sockfd) + " (group=" + peer_group + ") to " + peer_ip + ":" + std::to_string(peer_port));
                        }
                    }
                }

                // NEW: Automatically connect to servers from the list (limit to 8 peers)
                std::vector<std::tuple<std::string, std::string, int>> servers_to_connect;
                for (const auto &t : parsed_servers) {
                    const std::string &peer_group = std::get<0>(t);
                    const std::string &peer_ip = std::get<1>(t);
                    int peer_port = std::get<2>(t);
                    if (peer_group != group_id) {
                        servers_to_connect.emplace_back(peer_group, peer_ip, peer_port);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(connections_mutex);
                    int current_peer_count = 0;
                    for (const auto &c : connections) if (c.second.rfind("peer:",0)==0) current_peer_count++;

                    for (const auto &s : servers_to_connect) {
                        if (current_peer_count >= 8) break;

                        const std::string &peer_group = std::get<0>(s);
                        const std::string &peer_ip = std::get<1>(s);
                        int peer_port = std::get<2>(s);

                        // Dont connect to ourselves
                        if (peer_group == group_id) continue;

                        // Check if already connected to this group
                        bool already_connected = false;
                        for (const auto &conn : connections) {
                            if (conn.second == "peer:" + peer_group) { already_connected = true; break; }
                        }
                        if (!already_connected) {
                            log("Auto-connecting to peer " + peer_group + " at " + peer_ip + ":" + std::to_string(peer_port));
                            std::thread t(&TSAMServer::connectToPeer, this, peer_ip, peer_port);
                            
                            t.detach();
                            current_peer_count++;
                        }
                    }
                }
            }
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
            
            {
                std::lock_guard<std::mutex> lock(connections_mutex);
                std::lock_guard<std::mutex> lock2(peer_info_mutex);
                PeerInfo info;
                info.group_id = "unknown"; // Will be updated when we receive HELO
                info.ip = ip;
                info.port = port;
                peer_info[peer_sock] = info;
                connections[peer_sock] = "peer:unknown"; // Temporary until HELO
            }

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
                    log("Peer with IP " + ip + " and port " + std::to_string(port) + " sent SERVERS list: " + response);
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
                std::lock_guard<std::mutex> lock2(peer_info_mutex);
                connections.erase(peer_sock);
                peer_info.erase(peer_sock);
            }
            close(peer_sock);
            log("Closed connection to peer " + ip + ":" + std::to_string(port));
            
        } catch (const std::exception& e) {
            log("Failed to connect to peer " + ip + ":" + std::to_string(port) + " - " + std::string(e.what()));
        }
    }
    
public:
    // Initialize members in the order they are declared to avoid -Wreorder warnings
    TSAMServer(const std::string& id, int port) : running(false), server_port(port), group_id(id) {
        // Open logfile in append mode
        logfile.open("server.log", std::ios::app);
        if (!logfile.is_open()) {
            std::cerr << "Warning: failed to open server.log for writing\n";
        }
    }
    
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
        // Close logfile if open
        {
            std::lock_guard<std::mutex> lock(log_mutex);
            if (logfile.is_open()) logfile.close();
        }
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