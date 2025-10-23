// server.cpp - Single-threaded implementation with poll-based event loop
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <atomic>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstring>
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
        int requester_fd;  // client fd that requested this connection (for CONNECT command)
        std::chrono::steady_clock::time_point connection_start;  // when connection was established
        std::chrono::steady_clock::time_point last_keepalive_sent;  // track when we last sent KEEPALIVE to this peer
            std::chrono::steady_clock::time_point last_helo_time; // last time we saw a HELO from this peer
            int helo_count; // how many HELOs we've received from this peer (since state init)
            std::chrono::steady_clock::time_point last_bad_msg_time; // last time we logged a bad message from this peer
            int bad_msg_count; // number of bad/malformed messages observed in recent window
        
    ConnectionState() : buffer(""), handshake_complete(false), 
              is_peer(false), peer_name(""), peer_ip(""), peer_port(0), requester_fd(-1),
              connection_start(std::chrono::steady_clock::now()),
              last_keepalive_sent(std::chrono::steady_clock::now()),
              last_helo_time(std::chrono::steady_clock::time_point::min()), helo_count(0),
              last_bad_msg_time(std::chrono::steady_clock::time_point::min()), bad_msg_count(0) {}
    };
    
    std::map<int, ConnectionState> connection_states;

    // Message storage
    struct StoredMessage {
        std::string from_group;
        std::string to_group;
        std::string content;
        std::vector<std::string> hops;  // Hop list for routing (prevents loops)
        std::time_t timestamp;
    };
    std::map<std::string, std::deque<StoredMessage>> group_messages;
    // Simple recent message deduplication: store recent message signatures to avoid
    // forwarding loops or duplicate forwards. Key format: from|to|content
    std::deque<std::string> recent_messages; // acts as a small FIFO cache
    const size_t RECENT_MESSAGES_LIMIT = 256;
    // Counters to aggregate duplicate logs so we don't spam the log file and appear to loop
    std::map<std::string, int> duplicate_counts;
    std::map<std::string, std::chrono::steady_clock::time_point> duplicate_last_log;
    
    // File logging
    std::ofstream logfile;
    
    // Pending outgoing connections (non-blocking)
    struct PendingConnection {
        std::string ip;
        int port;
        int sockfd;
        std::string expected_peer_name;
        int requester_fd;  // client fd that requested this, -1 if none
    };
    std::vector<PendingConnection> pending_connections;
    
    // Known servers discovered via SERVERS messages (ip:port -> name)
    struct KnownServer {
        std::string name;
        bool is_student;  // true if name starts with A5_
    };
    std::map<std::string, KnownServer> known_servers;  // key = "ip:port"
    std::chrono::steady_clock::time_point last_server_check;
    
    // Blacklist for misbehaving peers (peer_name -> blacklist time)
    std::map<std::string, std::chrono::steady_clock::time_point> peer_blacklist;
    const int BLACKLIST_DURATION_SEC = 300;  // Block peer for 300 seconds = 5 minutes
    
    // IP-based rate limiter: track connection attempts from each IP
    // Tracks recent connection attempts to prevent rapid reconnection spam
    struct IPConnectionAttempt {
        int count;  // Number of connections in the window
        std::chrono::steady_clock::time_point first_time;  // Time of first connection
    };
    std::map<std::string, IPConnectionAttempt> ip_connection_attempts;  // key = IP address
    const int IP_RATELIMIT_WINDOW_SEC = 5;  // 5-second window
    const int IP_MAX_ATTEMPTS = 3;  // Max 3 connections in 5 seconds
    
    const int MIN_PEER_CONNECTIONS = 3;
    const int MAX_PEER_CONNECTIONS = 8;
    const int SERVER_CHECK_INTERVAL_SEC = 60;  // Retry every 60 seconds
    const int HELO_LOG_SUPPRESS_SEC = 45; // suppress repeated HELO logs for this many seconds
    const int BAD_MSG_LOG_WINDOW_SEC = 30; // window to count bad messages
    const int BAD_MSG_THRESHOLD = 5; // number of bad messages in window before blacklisting without noisy logs
    const int MAX_HOPS = 48;  // Maximum number of hops allowed (48 * 5 bytes = 240 bytes)
    const size_t MAX_HOP_LIST_SIZE = 240;  // Maximum bytes for hop list
    
    // Helper: Parse hops from a message string (looks for EOT delimiter)
    // Returns: pair<content_without_hops, vector_of_hops>
    std::pair<std::string, std::vector<std::string>> parseHops(const std::string& message) {
        size_t eot_pos = message.find(static_cast<char>(EOT));
        
        if (eot_pos == std::string::npos) {
            // No EOT - old format or no hops
            return {message, {}};
        }
        
        std::string content = message.substr(0, eot_pos);
        std::string hops_str = message.substr(eot_pos + 1);
        
        // Parse hops: "A5_9,A5_22,A5_43"
        std::vector<std::string> hops;
        if (!hops_str.empty()) {
            size_t start = 0;
            size_t comma_pos;
            while ((comma_pos = hops_str.find(',', start)) != std::string::npos) {
                std::string hop = hops_str.substr(start, comma_pos - start);
                hop = trim(hop);
                if (!hop.empty()) {
                    hops.push_back(hop);
                }
                start = comma_pos + 1;
            }
            // Last hop (or only hop if no commas)
            std::string last_hop = hops_str.substr(start);
            last_hop = trim(last_hop);
            if (!last_hop.empty()) {
                hops.push_back(last_hop);
            }
        }
        
        return {content, hops};
    }
    
    // Helper: Build hop string from vector
    std::string buildHopString(const std::vector<std::string>& hops) {
        if (hops.empty()) return "";
        
        std::string result;
        for (size_t i = 0; i < hops.size(); i++) {
            if (i > 0) result += ",";
            result += hops[i];
        }
        return result;
    }
    
    // Helper: Check if a group is in the hop list
    bool isInHops(const std::string& group, const std::vector<std::string>& hops) {
        for (const auto& hop : hops) {
            if (hop == group) return true;
        }
        return false;
    }
    
    void log(const std::string& message) {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::ostringstream oss;
        oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << message;
        std::string line = oss.str();

        std::cout << line << std::endl;
        if (logfile.is_open()) {
            logfile << line << std::endl;
            logfile.flush();
        }
    }
    
    // Set socket to non-blocking mode
    void setNonBlocking(int sockfd) {
        int flags = fcntl(sockfd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        }
    }
    
    // Trim whitespace from both ends of a string
    static std::string trim(const std::string &s) {
        size_t start = 0;
        while (start < s.size() && isspace((unsigned char)s[start])) ++start;
        if (start == s.size()) return "";
        size_t end = s.size() - 1;
        while (end > start && isspace((unsigned char)s[end])) --end;
        return s.substr(start, end - start + 1);
    }

    // Normalize a peer/group name received from HELO or SERVERS: trim and remove stray commas/semicolons
    static std::string normalizeName(const std::string &raw) {
        std::string n = trim(raw);
        // Remove any internal control chars, semicolons, or extra commas
        // Replace spaces with underscores (common error in peer implementations)
        std::string out;
        for (char c : n) {
            if (c == ';' || c == '\r' || c == '\n') continue;
            // Keep commas only if they are part of the name (unlikely); prefer to stop at first comma
            if (c == ',') break;
            // Replace spaces with underscores
            if (c == ' ') {
                out.push_back('_');
            } else {
                out.push_back(c);
            }
        }
        return trim(out);
    }
    
    // Find a known peer by ip:port (return peer_name if found, empty string otherwise)
    std::string findPeerNameByIpPort(const std::string& ip, int port) {
        for (const auto& cs : connection_states) {
            if (cs.second.is_peer && cs.second.peer_ip == ip && cs.second.peer_port == port && !cs.second.peer_name.empty()) {
                return cs.second.peer_name;
            }
        }
        return "";
    }
    
    // Add or update a known server discovered from SERVERS
    void addKnownServer(const std::string& name, const std::string& ip, int port) {
        std::string key = ip + ":" + std::to_string(port);
        bool is_student = (name.rfind("A5_", 0) == 0);
        known_servers[key] = {name, is_student};
    }
    
    // Check if a peer is blacklisted (expired blacklist entries are cleaned up)
    bool isPeerBlacklisted(const std::string& peer_name) {
        auto it = peer_blacklist.find(peer_name);
        if (it == peer_blacklist.end()) {
            return false;  // Not blacklisted
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
        
        if (elapsed >= BLACKLIST_DURATION_SEC) {
            // Blacklist expired - remove it
            peer_blacklist.erase(it);
            return false;
        }
        
        return true;  // Still blacklisted
    }
    
    // Blacklist a peer for misbehavior
    void blacklistPeer(const std::string& peer_name) {
        peer_blacklist[peer_name] = std::chrono::steady_clock::now();
        log("BLACKLISTED peer " + peer_name + " for " + std::to_string(BLACKLIST_DURATION_SEC) + " seconds");
    }
    
    // Check if an IP is rate-limited (too many rapid connection attempts)
    bool isIPRateLimited(const std::string& ip) {
        auto it = ip_connection_attempts.find(ip);
        if (it == ip_connection_attempts.end()) {
            return false;  // Not rate limited
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.first_time).count();
        
        if (elapsed >= IP_RATELIMIT_WINDOW_SEC) {
            // Window expired - remove entry
            ip_connection_attempts.erase(it);
            return false;
        }
        
        // Still in window - check if over limit
        return it->second.count >= IP_MAX_ATTEMPTS;
    }
    
    // Record a connection attempt from an IP
    void recordIPConnectionAttempt(const std::string& ip) {
        auto it = ip_connection_attempts.find(ip);
        if (it == ip_connection_attempts.end()) {
            // First connection attempt
            IPConnectionAttempt attempt;
            attempt.count = 1;
            attempt.first_time = std::chrono::steady_clock::now();
            ip_connection_attempts[ip] = attempt;
        } else {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.first_time).count();
            
            if (elapsed >= IP_RATELIMIT_WINDOW_SEC) {
                // Window expired - reset
                it->second.count = 1;
                it->second.first_time = now;
            } else {
                // Still in window - increment count
                it->second.count++;
            }
        }
    }
    
    // Try to maintain peer connections (called every 60 seconds)
    void maintainPeerConnections(std::vector<pollfd>& fds) {
        // Count current connections and categorize them
        int peer_count = 0;
        // if we find our group id e.g. ourselves A5_29 then -1 to peer count
        if (connection_states.size() > 0) {
            for (const auto &cs : connection_states) {
                if (cs.second.is_peer && cs.second.handshake_complete && cs.second.peer_name == group_id) {
                    peer_count--;
                    break;
                }
            }
        }
        int student_count = 0;
        std::vector<int> instructor_fds;  // Track all instructor connections
        
        for (const auto &cs : connection_states) {
            if (cs.second.is_peer && cs.second.handshake_complete) {
                peer_count++;
                if (cs.second.peer_name.rfind("A5_", 0) == 0) {
                    student_count++;
                } else {
                    instructor_fds.push_back(cs.first);
                }
            }
        }
        
        log("Peer check: " + std::to_string(peer_count) + " connected (" + 
            std::to_string(student_count) + " students, " + std::to_string(instructor_fds.size()) + " instructors)");
        
        // Identify Instr_1 and other instructors
        bool has_instr1 = false;
        for (int fd : instructor_fds) {
            if (connection_states[fd].peer_name == "Instr_1") {
                has_instr1 = true;
                break;
            }
        }
        
        // ONLY drop instructors if we're at capacity (>= MAX_PEER_CONNECTIONS)
        // This avoids unnecessarily dropping useful connections when we have free slots
        if (peer_count >= MAX_PEER_CONNECTIONS) {
            // At capacity - drop instructors except Instr_1 to make room for students
            for (int fd : instructor_fds) {
                if (connection_states[fd].peer_name == "Instr_1") {
                    log("Keeping Instr_1 connection for reliable message forwarding");
                } else {
                    log("At capacity (" + std::to_string(peer_count) + "/" + std::to_string(MAX_PEER_CONNECTIONS) + 
                        ") - dropping instructor " + connection_states[fd].peer_name + " to prioritize students");
                    closeConnection(fd, fds);
                    peer_count--;
                }
            }
        } else {
            // Below capacity - keep all instructors (including Instr_1)
            log("Below capacity (" + std::to_string(peer_count) + "/" + std::to_string(MAX_PEER_CONNECTIONS) + 
                ") - keeping all instructor connections");
        }
        
        // Ensure we're connected to Instr_1 for message forwarding
        if (!has_instr1) {
            // Check if Instr_1 is in known_servers
            bool instr1_known = false;
            for (const auto& entry : known_servers) {
                if (entry.second.name == "Instr_1") {
                    instr1_known = true;
                    break;
                }
            }
            
            // If not known, add it with default address
            if (!instr1_known) {
                addKnownServer("Instr_1", "130.208.246.98", 5001);
                log("Added Instr_1 to known_servers for reliable forwarding");
            }
        }
        
        // PRIORITY 1: ALWAYS ensure we have a connection to Instr_1 for reliable message forwarding
        // Instr_1 gets reserved even if we're near capacity
        if (!has_instr1 && peer_count < MAX_PEER_CONNECTIONS) {
            // Check if pending connection to Instr_1
            bool pending = false;
            for (const auto &pc : pending_connections) {
                if (pc.ip == "130.208.246.98" && pc.port == 5001) {
                    pending = true;
                    break;
                }
            }
            
            if (!pending) {
                log("PRIORITY: Connecting to Instr_1 for reliable message forwarding");
                initiatePeerConnection("130.208.246.98", 5001, fds, -1, "Instr_1");
                peer_count++;
            }
        }
        
        // PRIORITY 2: Connect to students in remaining slots
        if (peer_count < MAX_PEER_CONNECTIONS) {
            // Connect to all available students
            for (const auto& entry : known_servers) {
                if (peer_count >= MAX_PEER_CONNECTIONS) break;
                
                const KnownServer& srv = entry.second;
                if (!srv.is_student) continue;  // Only students
                
                // Skip if already connected
                bool connected = false;
                for (const auto &cs : connection_states) {
                    if (cs.second.is_peer && cs.second.peer_name == srv.name) {
                        connected = true;
                        break;
                    }
                }
                
                if (!connected) {
                    size_t colon_pos = entry.first.find(':');
                    std::string ip = entry.first.substr(0, colon_pos);
                    int port = std::stoi(entry.first.substr(colon_pos + 1));
                    
                    // Skip if pending
                    bool pending = false;
                    for (const auto &pc : pending_connections) {
                        if (pc.ip == ip && pc.port == port) {
                            pending = true;
                            break;
                        }
                    }
                    
                    if (!pending) {
                        log("Connecting to student: " + srv.name);
                        initiatePeerConnection(ip, port, fds, -1, srv.name);
                        peer_count++;
                    }
                }
            }
        }
    }
    
    // Initiate non-blocking connection to peer
    void initiatePeerConnection(const std::string& ip, int port, std::vector<pollfd>& fds, int requester_fd = -1, const std::string& expected_peer_name = "") {
        // Deduplicate check: avoid multiple simultaneous connects to same ip:port
        for (const auto &pc : pending_connections) {
            if (pc.ip == ip && pc.port == port) {
                log("Skipping duplicate pending connect to " + ip + ":" + std::to_string(port));
                return;
            }
        }
        for (const auto &cs : connection_states) {
            if (cs.second.is_peer && cs.second.peer_ip == ip && cs.second.peer_port == port) {
                log("Skipping duplicate connect: already connected to " + ip + ":" + std::to_string(port));
                return;
            }
        }
        
        try {
            // Create socket
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) {
                log("Failed to create socket for peer connection");
                return;
            }
            
            // Set non-blocking
            setNonBlocking(sockfd);
            
            // Connect (will return immediately with EINPROGRESS)
            sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
            
            int result = connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr));
            
            if (result == 0) {
                // Connected immediately (unlikely)
                log("Connected immediately to " + ip + ":" + std::to_string(port));
                handleNewPeerConnection(sockfd, ip, port, fds, expected_peer_name, requester_fd);
            } else if (errno == EINPROGRESS) {
                // Connection in progress - add to poll with POLLOUT
                pollfd pfd;
                pfd.fd = sockfd;
                pfd.events = POLLOUT;
                fds.push_back(pfd);
                
                PendingConnection pc;
                pc.ip = ip;
                pc.port = port;
                pc.sockfd = sockfd;
                pc.expected_peer_name = expected_peer_name;
                pc.requester_fd = requester_fd;
                pending_connections.push_back(pc);
                
                log("Connecting to " + ip + ":" + std::to_string(port));
            } else {
                log("Failed to connect to " + ip + ":" + std::to_string(port));
                close(sockfd);
            }
        } catch (const std::exception& e) {
            log("Exception connecting to peer: " + std::string(e.what()));
        }
    }
    
    // Handle newly established peer connection
    void handleNewPeerConnection(int sockfd, const std::string& ip, int port, std::vector<pollfd>& fds, const std::string& expected_peer_name = "", int requester_fd = -1) {
        log("handleNewPeerConnection to " + ip + ":" + std::to_string(port));
        
        // Initialize connection state
        ConnectionState state;
        state.is_peer = true;
        state.peer_ip = ip;
        state.peer_port = port;
        // Set peer_name from expected name (from SERVERS) if provided
        state.peer_name = expected_peer_name;
        state.handshake_complete = false;
        state.requester_fd = requester_fd;
        connection_states[sockfd] = state;
        
        // If we know the peer name, add to known_servers immediately
        if (!expected_peer_name.empty()) {
            addKnownServer(expected_peer_name, ip, port);
        }
        
        // Send HELO
        std::string helo = "HELO," + group_id;
        Networking::sendMessage(sockfd, helo);
        // Log which peer (ip:port) and fd we sent HELO to so it's unambiguous
        log("HELO sent to peer (" + ip + ":" + std::to_string(port) + ") fd=" + std::to_string(sockfd) + ": " + helo);
            
        // Add to poll for reading
        bool found = false;
        for (auto& pfd : fds) {
            if (pfd.fd == sockfd) {
                pfd.events = POLLIN;
                found = true;
                break;
            }
        }
        
        if (!found) {
            pollfd pfd;
            pfd.fd = sockfd;
            pfd.events = POLLIN;
            fds.push_back(pfd);
        }
    }

    // Process message for an existing connection
    void processMessage(int sockfd, const std::string& message, std::vector<pollfd>& fds) {
        ConnectionState& state = connection_states[sockfd];
        
        if (!state.handshake_complete) {
            // First message - determine if peer or client
            if (message.rfind("HELO,", 0) == 0 || state.is_peer) {
                // Server-to-server: HELO,<FROM_GROUP_ID>
                std::string peerName = message.substr(5);
                peerName = normalizeName(peerName);
                
                // Validate: peer name must be at least 3 characters (e.g., "A5_X" or "Instr_1")
                // Reject obviously corrupt names like "RS" or "E"
                if (peerName.length() < 3) {
                    log("WARNING: Received HELO with suspiciously short peer name: '" + peerName + "' (likely frame corruption)");
                    // Don't update peer_name or handshake, just skip this message
                    // Connection will eventually timeout or get corrected by next HELO
                    return;
                }
                
                // CHECK BLACKLIST BEFORE ACCEPTING HANDSHAKE
                if (isPeerBlacklisted(peerName)) {
                    // log("Rejecting BLACKLISTED peer " + peerName + " during handshake - closing connection");
                    closeConnection(sockfd, fds);
                    return;
                }
                
                state.is_peer = true;
                
                // Trust what the peer announces - don't try to guess from old connections
                state.peer_name = peerName;
                state.handshake_complete = true;
                
                log("Registered peer server: " + state.peer_name + " on fd=" + std::to_string(sockfd));

                // If this connection was requested by a client, notify them now
                if (state.requester_fd != -1) {
                    if (connection_states.find(state.requester_fd) != connection_states.end()) {
                        std::string msg = "CONNECTED to " + state.peer_ip + ":" + std::to_string(state.peer_port) + " (peer: " + state.peer_name + ")";
                        Networking::sendMessage(state.requester_fd, msg);
                        log("Notified requester fd=" + std::to_string(state.requester_fd) + " of successful connection to " + state.peer_name);
                    }
                    state.requester_fd = -1;  // Clear after notifying
                }

                // Respond with HELO back
                std::string helo_reply = "HELO," + group_id;
                Networking::sendMessage(sockfd, helo_reply);
                log("Sent HELO response to peer " + state.peer_name + ": " + helo_reply);

                // Send SERVERS list
                sockaddr_in local;
                socklen_t llen = sizeof(local);
                if (getsockname(sockfd, (sockaddr*)&local, &llen) == 0) {
                    char buf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
                    std::string host_ip = std::string(buf);

                    // Build SERVERS list: include ourselves first
                    std::string servers_reply = "SERVERS,";
                    servers_reply += group_id + "," + host_ip + "," + std::to_string(server_port) + ";";

                    // Include all currently connected peers that have a known name and port,
                    // but avoid duplicate entries (same name + ip:port).
                    std::set<std::string> seen_entries;
                    for (const auto &cs : connection_states) {
                        if (cs.second.is_peer && cs.second.handshake_complete && !cs.second.peer_name.empty() && cs.second.peer_port > 0) {
                            std::string key = cs.second.peer_name + "|" + cs.second.peer_ip + ":" + std::to_string(cs.second.peer_port);
                            if (seen_entries.insert(key).second) {
                                servers_reply += cs.second.peer_name + "," + cs.second.peer_ip + "," + std::to_string(cs.second.peer_port) + ";";
                            }
                        }
                    }

                    Networking::sendMessage(sockfd, servers_reply);
                    log("Sent SERVERS response to peer " + state.peer_name + ": " + servers_reply);
                }
            } else {
                // Client connection
                state.is_peer = false;
                state.handshake_complete = true;
                log("Connection identified as client on fd=" + std::to_string(sockfd));
                std::string resp = processClientCommand(message, fds, sockfd);
                if (!resp.empty()) {
                    Networking::sendMessage(sockfd, resp);
                    log("Sent to client: " + resp);
                }
            }
        } else {
            // Subsequent messages
            // For peer messages, parse and strip hops before logging for cleaner output
            std::string log_message = message;
            if (state.is_peer && message.find(static_cast<char>(EOT)) != std::string::npos) {
                std::pair<std::string, std::vector<std::string>> parsed = parseHops(message);
                log_message = parsed.first;
                if (!parsed.second.empty()) {
                    log_message += " [hops: " + buildHopString(parsed.second) + "]";
                }
            }
            log("Received from connection " + std::string(state.is_peer ? ("peer: " + state.peer_name) : "client") + ": " + log_message);

            if (state.is_peer) {
                // Detect client-only commands being sent to peer socket - indicates frame corruption or misbehavior
                // Note: SENDMSG is a VALID peer-to-peer command for message forwarding, not a client command!
                if (message == "GETMSG" || message == "GETALLMSG" ||
                    message == "LISTSERVERS" || message.rfind("CONNECT ", 0) == 0) {
                    // Rate-limit noisy logs for peers that repeatedly send client-only commands
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - state.last_bad_msg_time).count();
                    if (elapsed > BAD_MSG_LOG_WINDOW_SEC) {
                        state.bad_msg_count = 1;
                        state.last_bad_msg_time = now;
                    } else {
                        state.bad_msg_count++;
                    }

                    if (state.bad_msg_count >= BAD_MSG_THRESHOLD) {
                        log("Frame corruption detected (repeated): peer " + state.peer_name + " sent client-only command '" + message + "' - BLACKLISTING and closing connection");
                        blacklistPeer(state.peer_name);
                        closeConnection(sockfd, fds);
                        return;
                    } else {
                        // Log once and then suppress further per-message logs until threshold or window expiry
                        log("Frame corruption detected: peer " + state.peer_name + " sent client-only command '" + message + "' - incrementing bad_msg_count=" + std::to_string(state.bad_msg_count));
                        return;
                    }
                }
                
                std::string response = processServerCommand(message, sockfd, fds);
                if (!response.empty()) {
                    Networking::sendMessage(sockfd, response);
                    log("Sent reply to peer: " + response);
                }
            } else {
                // Enforce that clients only send client commands. If a client sends a
                // server-only command, treat it as a protocol violation and close the connection.
                if (message.rfind("HELO,", 0) == 0 || message.rfind("SERVERS,", 0) == 0 ||
                    message.rfind("KEEPALIVE,", 0) == 0 || message.rfind("GETMSGS,", 0) == 0 ||
                    message.rfind("STATUSREQ", 0) == 0 || message.rfind("STATUSRESP,", 0) == 0) {
                    log("Protocol violation: client sent server-only command: " + message + " - closing connection fd=" + std::to_string(sockfd));
                    closeConnection(sockfd, fds);
                    return;
                }

                std::string response = processClientCommand(message, fds, sockfd);
                if (!response.empty()) {
                    Networking::sendMessage(sockfd, response);
                    log("Sent reply to client: " + response);
                }
            }
        }
    }
    
    // Handle CLIENT commands
    std::string processClientCommand(const std::string& command, std::vector<pollfd>& fds, int requester_fd) {
        if (command == "GETMSG") {
            auto &q = group_messages[group_id];
            if (q.empty()) {
                return "NO_MESSAGES";
            }
            StoredMessage msg = q.front();
            q.pop_front();
            return "MESSAGE: From: " + msg.from_group + " Msg: " + msg.content;
        }
        else if (command == "GETALLMSG") {
            auto &q = group_messages[group_id];
            if (q.empty()) {
                return "NO_MESSAGES";
            }
            std::string result = "MESSAGES,";
            size_t count = 0;
            while (!q.empty() && count < 10) {  // Limit to 10 messages
                StoredMessage msg = q.front();
                q.pop_front();
                if (count > 0) result += "|";  // Delimiter between messages
                result += "From:" + msg.from_group + ",Msg:" + msg.content;
                count++;
            }
            return result;
        }
        else if (command.find("SENDMSG,") == 0) {
            size_t first_comma = command.find(',');
            size_t second_comma = command.find(',', first_comma + 1);
            
            if (second_comma != std::string::npos) {
                std::string to_group = command.substr(first_comma + 1, second_comma - first_comma - 1);
                std::string message = command.substr(second_comma + 1);

                // Sanitize group name: remove ONLY control characters and spaces
                // This prevents corruption from stray UTF-8 but preserves valid underscores/dashes
                std::string sanitized_group;
                for (unsigned char c : to_group) {
                    // Skip control characters and spaces, but keep alphanumeric, underscore, dash
                    if (c >= 32 && c < 127 && c != ' ') {  // Printable ASCII except space
                        sanitized_group += c;
                    }
                }
                to_group = sanitized_group;

                // Validate group name
                if (to_group.empty()) {
                    return "ERROR,INVALID_GROUP_NAME";
                }
                
                // STORE ALL messages in mailbox (hybrid store-and-forward approach)
                // This ensures we can serve messages via GETMSGS/STATUSREQ
                StoredMessage sm;
                sm.from_group = group_id;
                sm.to_group = to_group;
                sm.content = message;
                sm.hops = {group_id};  // Initialize hops with originating group
                sm.timestamp = std::time(nullptr);
                group_messages[to_group].push_back(sm);
                log("Stored message from " + group_id + " for group " + to_group + ": " + message);

                // Use deduplication cache to avoid duplicate forwards/loops.
                // Use same signature format as peer SENDMSG handler for consistency.
                std::string signature = group_id + "|" + to_group + "|" + message;

                bool seen = false;
                for (const auto &s : recent_messages) {
                    if (s == signature) { 
                        seen = true;
                        break;
                    }
                }
                if (!seen) {
                    // Add to recent cache
                    recent_messages.push_back(signature);
                    if (recent_messages.size() > RECENT_MESSAGES_LIMIT) {
                        recent_messages.pop_front();
                    }

                    // ONLY forward if message is NOT for us
                    if (to_group != group_id) {
                        // Apply 3-tier routing logic with hop tracking:
                        // 1. If connected to destination: send directly + DELETE from queue
                        // 2. If NOT connected but known: forward to Instr_1 + DELETE from queue
                        // 3. If unknown: keep in queue for GETMSGS retrieval
                        
                        // Build hop list for forwarding (append our group_id)
                        std::vector<std::string> forward_hops = sm.hops;
                        
                        // Check if we've exceeded max hops
                        if (forward_hops.size() >= static_cast<size_t>(MAX_HOPS)) {
                            log("Max hops reached (" + std::to_string(MAX_HOPS) + ") - keeping in queue");
                            // In full implementation: clear hops, schedule 30-min retry
                            // For now: just keep in queue
                        } else {
                            // Tier 1: Check if we're directly connected to the destination
                            int destination_fd = -1;
                            for (const auto& cs : connection_states) {
                                if (cs.second.is_peer && cs.second.handshake_complete && 
                                    cs.second.peer_name == to_group) {
                                    // Check if destination is already in hops (loop prevention)
                                    if (!isInHops(to_group, forward_hops)) {
                                        destination_fd = cs.first;
                                    }
                                    break;
                                }
                            }
                            
                            if (destination_fd != -1) {
                                // Tier 1: Direct delivery - send with hops and DELETE from queue
                                std::string hop_str = buildHopString(forward_hops);
                                std::string peer_msg = "SENDMSG," + to_group + "," + group_id + "," + message + static_cast<char>(EOT) + hop_str;
                                bool sent_ok = Networking::sendMessage(destination_fd, peer_msg);
                                if (sent_ok) {
                                    log("TIER 1: Forwarded SENDMSG directly to destination " + to_group + " with hops - DELETING from queue");
                                    // Delete from queue (message delivered)
                                    group_messages[to_group].pop_back();
                                } else {
                                    log("TIER 1: Failed to send SENDMSG to destination fd=" + std::to_string(destination_fd) + " - keeping in queue");
                                    // Keep message in queue for retry; do not delete
                                }
                            } else {
                                // Check if destination is known
                                bool destination_is_known = false;
                                for (const auto& entry : known_servers) {
                                    if (entry.second.name == to_group) {
                                        destination_is_known = true;
                                        break;
                                    }
                                }
                                
                                if (destination_is_known) {
                                    // Tier 2: Known but not connected - forward to Instr_1 and DELETE
                                    int instr1_fd = -1;
                                    for (const auto& cs : connection_states) {
                                        if (cs.second.is_peer && cs.second.handshake_complete && 
                                            cs.second.peer_name == "Instr_1") {
                                            // Check if Instr_1 is already in hops (loop prevention)
                                            if (!isInHops("Instr_1", forward_hops)) {
                                                instr1_fd = cs.first;
                                            }
                                            break;
                                        }
                                    }
                                    
                                    if (instr1_fd != -1) {
                                        std::string hop_str = buildHopString(forward_hops);
                                        std::string peer_msg = "SENDMSG," + to_group + "," + group_id + "," + message + static_cast<char>(EOT) + hop_str;
                                        bool sent_ok = Networking::sendMessage(instr1_fd, peer_msg);
                                        if (sent_ok) {
                                            log("TIER 2: Destination " + to_group + " known but not connected - forwarded to Instr_1 with hops - DELETING from queue");
                                            // Delete from queue (delegated to Instr_1)
                                            group_messages[to_group].pop_back();
                                        } else {
                                            log("TIER 2: Failed to send SENDMSG to Instr_1 fd=" + std::to_string(instr1_fd) + " - keeping in queue");
                                        }
                                    } else {
                                        log("TIER 2: Destination " + to_group + " known but Instr_1 not connected or in hop list - KEEPING in queue");
                                    }
                                } else {
                                    // Tier 3: Unknown destination - keep in queue
                                    log("TIER 3: Destination " + to_group + " unknown - KEEPING in queue for GETMSGS retrieval");
                                }
                            }
                        }
                    } else {
                        log("Message is for us (" + group_id + ") - stored locally, not forwarding");
                    }
                } else {
                    log("Duplicate SENDMSG from client IGNORED (recent): " + signature);
                }
                
                return "MESSAGE_SENT";
            }
            return "ERROR,INVALID_FORMAT";
        }
        else if (command == "LISTSERVERS") {
            std::string list = "SERVERS,";
            
            // Include ourselves first
            list += group_id + "," + "130.208.246.98" + "," + std::to_string(server_port) + ";";
            
            // Keep track of which peer names we've already added to avoid duplicates
            std::set<std::string> seen_peers;
            seen_peers.insert(group_id);  // Mark ourselves as seen
            
            for (const auto& cs : connection_states) {
                if (cs.second.is_peer && cs.second.handshake_complete) {
                    // Only include peers with valid name and listening port
                    if (!cs.second.peer_name.empty() && cs.second.peer_port > 0 &&
                        seen_peers.find(cs.second.peer_name) == seen_peers.end()) {
                        list += cs.second.peer_name + "," + cs.second.peer_ip + "," + 
                               std::to_string(cs.second.peer_port) + ";";
                        seen_peers.insert(cs.second.peer_name);
                    }
                }
            }
            return list;
        }
        else if (command.rfind("CONNECT ", 0) == 0) {
            size_t space_pos = command.find(' ', 8);
            if (space_pos == std::string::npos) {
                return "ERROR,INVALID_FORMAT (usage: CONNECT <ip> <port>)";
            }
            
            std::string ip = command.substr(8, space_pos - 8);
            std::string port_str = command.substr(space_pos + 1);
            
            try {
                int port = std::stoi(port_str);
                
                // Check if already connected to this ip:port
                bool already_connected = false;
                for (const auto &pc : pending_connections) {
                    if (pc.ip == ip && pc.port == port) {
                        already_connected = true;
                        break;
                    }
                }
                if (!already_connected) {
                    for (const auto &cs : connection_states) {
                        if (cs.second.is_peer && cs.second.peer_ip == ip && cs.second.peer_port == port) {
                            already_connected = true;
                            break;
                        }
                    }
                }
                
                if (already_connected) {
                    return "ERROR,ALREADY_CONNECTED to " + ip + ":" + std::to_string(port);
                }
                
                initiatePeerConnection(ip, port, fds, requester_fd);
                // Do not reply now — only notify the client after the outbound
                // connection completes successfully.
                return "";
            } catch (...) {
                return "ERROR,INVALID_PORT";
            }
        }
        return "ERROR,UNKNOWN_COMMAND";
    }

    // Handle SERVER-to-SERVER commands
    std::string processServerCommand(const std::string& command, int sockfd, std::vector<pollfd>& fds) {
        if (command.rfind("HELO,", 0) == 0) {
            std::string from_group = command.substr(5);
            from_group = normalizeName(from_group);
            log("Received HELO from " + from_group);
            
            // Check if this peer is blacklisted
            if (isPeerBlacklisted(from_group)) {
                log("Rejecting connection from BLACKLISTED peer " + from_group + " - closing connection");
                closeConnection(sockfd, fds);
                return "";
            }
            
            if (sockfd >= 0) {
                ConnectionState& state = connection_states[sockfd];

                // Allow HELO updates even after handshake - peer may have reconnected with new info
                // Only update peer_name if not already set from outbound connect.
                // If it differs from what we know (from SERVERS), log a warning but trust the HELO.
                if (state.peer_name.empty()) {
                    state.peer_name = from_group;
                    log("Set peer_name to " + from_group + " from HELO");
                } else if (state.peer_name != from_group) {
                    log("HELO peer name '" + from_group + "' differs from expected name '" + state.peer_name + "' (updating to HELO name)");
                    state.peer_name = from_group;
                }

                // If this is the initial HELO (handshake not yet completed), reply with HELO and SERVERS.
                // If handshake is already complete, treat this as an informational update and do NOT reply
                // (replying to every HELO causes symmetric endpoints to echo HELO/SERVERS indefinitely).
                bool was_handshake_complete = state.handshake_complete;
                state.handshake_complete = true;

                // Track HELO frequency and only log noisy updates when needed
                state.helo_count++;
                auto now = std::chrono::steady_clock::now();
                bool time_threshold = (state.last_helo_time == std::chrono::steady_clock::time_point::min()) ||
                                      (std::chrono::duration_cast<std::chrono::seconds>(now - state.last_helo_time).count() > HELO_LOG_SUPPRESS_SEC);

                if (!was_handshake_complete) {
                    std::string helo_reply = "HELO," + group_id;
                    Networking::sendMessage(sockfd, helo_reply);
                    log("Sent HELO response: " + helo_reply);

                    // Determine the local IP address used on this connection and send it in the
                    // SERVERS reply so the peer knows how to reach us.
                    sockaddr_in local;
                    socklen_t llen = sizeof(local);
                    std::string host_ip = "130.208.246.98";
                    if (getsockname(sockfd, (sockaddr*)&local, &llen) == 0) {
                        char buf[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
                        host_ip = std::string(buf);
                    }
                    std::string servers_reply = "SERVERS," + group_id + "," + host_ip + "," + std::to_string(server_port) + ";";
                    Networking::sendMessage(sockfd, servers_reply);
                    log("Sent SERVERS response: " + servers_reply);
                } else {
                    // Log only if peer name changed or it's been a while since last HELO
                    if (state.peer_name != from_group) {
                        log("HELO update: peer name changed to '" + from_group + "' (was '" + state.peer_name + "')");
                        state.peer_name = from_group;
                    } else if (time_threshold) {
                        log("Received periodic HELO from " + state.peer_name + " - suppressing reply to avoid echo loop");
                    }
                    // Otherwise silently accept the HELO update without logging to reduce noise
                }

                state.last_helo_time = now;

                return "";
            }
            return "HELO," + group_id;
        }
    else if (command.rfind("SENDMSG,", 0) == 0) {
            size_t start = 8;
            size_t p1 = command.find(',', start);
            if (p1 == std::string::npos) return "";
            size_t p2 = command.find(',', p1 + 1);
            if (p2 == std::string::npos) return "";

            std::string to_group = command.substr(start, p1 - start);
            std::string from_group = command.substr(p1 + 1, p2 - p1 - 1);
            std::string message = command.substr(p2 + 1);

            // Parse hops from message (looks for EOT delimiter)
            std::pair<std::string, std::vector<std::string>> parsed = parseHops(message);
            std::string content = parsed.first;
            std::vector<std::string> hops = parsed.second;
            message = content;  // Update message to be content without hops
            
            // Log received hops for debugging
            if (!hops.empty()) {
                log("Received SENDMSG with hops: " + buildHopString(hops));
            }

            // Sanitize group names: remove ONLY control characters and spaces
            // This prevents corruption from stray UTF-8 but preserves valid underscores/dashes
            auto sanitize = [](std::string &s) {
                std::string sanitized;
                for (unsigned char c : s) {
                    if (c >= 32 && c < 127 && c != ' ') {  // Printable ASCII except space
                        sanitized += c;
                    }
                }
                s = sanitized;
            };
            sanitize(to_group);
            sanitize(from_group);

             // VALIDATION: Reject malformed group names
            if (to_group.find(' ') != std::string::npos || from_group.find(' ') != std::string::npos) {
                log("Rejecting malformed SENDMSG - group names contain spaces: " + command);
                return "";
            }

            // VALIDATION: Reject empty group names
            if (to_group.empty() || from_group.empty()) {
                log("Rejecting SENDMSG with empty group name");
                return "";
            }

            // VALIDATION: Reject our own reflected messages  
            if (from_group == group_id) {
                log("Ignoring our own reflected message");
                return "";
            }

            // Store the message for the destination group (we act as a message holder/relay)
            StoredMessage stored_msg;
            stored_msg.from_group = from_group;
            stored_msg.to_group = to_group;
            stored_msg.content = message;
            stored_msg.hops = hops;  // Store the received hops
            stored_msg.timestamp = std::time(nullptr);
            
            // Deduplicate using recent_messages with improved signature
            // Extract just the quote part (before any routing list) for better dedup
            // Format: "quote text"AuthorA5_14,A5_27,... so find the last quote
            std::string sig_base = message;
            size_t last_quote = message.rfind('"');
            if (last_quote != std::string::npos && last_quote < message.length() - 1) {
                // Take everything up to ~50 chars after the closing quote (captures author name)
                size_t end_pos = std::min(last_quote + 50, message.length());
                sig_base = message.substr(0, end_pos);
            } else if (sig_base.length() > 200) {
                // Fallback: truncate long messages
                sig_base = sig_base.substr(0, 200);
            }
            std::string signature = from_group + "|" + to_group + "|" + sig_base;
            
            bool seen = false;
            for (const auto &s : recent_messages) {
                if (s == signature) { seen = true; break; }
            }
            
            if (!seen) {
                // Add to cache and store
                recent_messages.push_back(signature);
                if (recent_messages.size() > RECENT_MESSAGES_LIMIT) recent_messages.pop_front();

                // Store message for the destination group (even if it's not us)
                group_messages[to_group].push_back(stored_msg);
                log("Stored message for group " + to_group + " from " + from_group);

                // Apply 3-tier routing logic with hop tracking:
                // 1. If message is for us: just store, don't forward
                // 2. If connected to destination: send directly + DELETE from queue
                // 3. If NOT connected but known: forward to Instr_1 + DELETE from queue
                // 4. If unknown: keep in queue for GETMSGS retrieval
                
                if (to_group != group_id) {
                    // Check if we're already in the hop list (loop detection)
                    if (isInHops(group_id, stored_msg.hops)) {
                        log("Loop detected: " + group_id + " already in hops - NOT forwarding");
                        // Don't forward, just keep stored for potential GETMSGS retrieval
                        return "";
                    }
                    
                    // Append our group_id to hops before forwarding
                    std::vector<std::string> forward_hops = stored_msg.hops;
                    forward_hops.push_back(group_id);
                    
                    // Check if we've exceeded max hops
                    if (forward_hops.size() >= static_cast<size_t>(MAX_HOPS)) {
                        log("Max hops reached (" + std::to_string(MAX_HOPS) + ") - keeping in queue");
                        // In full implementation: clear hops, schedule 30-min retry
                        return "";
                    }
                    
                    // Tier 1: Check if we're directly connected to the destination
                    int destination_fd = -1;
                    for (const auto& cs : connection_states) {
                        if (cs.second.is_peer && cs.second.handshake_complete && 
                            cs.second.peer_name == to_group) {
                            // Check if destination is already in hops (additional loop prevention)
                            if (!isInHops(to_group, forward_hops)) {
                                destination_fd = cs.first;
                            }
                            break;
                        }
                    }
                    
                    if (destination_fd != -1) {
                        // Tier 1: Direct delivery - send with updated hops and DELETE from queue
                        std::string hop_str = buildHopString(forward_hops);
                        std::string forward_msg = "SENDMSG," + to_group + "," + from_group + "," + message + static_cast<char>(EOT) + hop_str;
                        bool sent_ok = Networking::sendMessage(destination_fd, forward_msg);
                        if (sent_ok) {
                            log("TIER 1: Forwarded SENDMSG directly to destination " + to_group + " with hops - DELETING from queue");
                            // Delete from queue (message delivered)
                            group_messages[to_group].pop_back();
                        } else {
                            log("TIER 1: Failed to send SENDMSG to destination fd=" + std::to_string(destination_fd) + " - keeping in queue");
                        }
                    } else {
                        // Check if destination is known
                        bool destination_is_known = false;
                        for (const auto& entry : known_servers) {
                            if (entry.second.name == to_group) {
                                destination_is_known = true;
                                break;
                            }
                        }
                        
                        if (destination_is_known) {
                            // Tier 2: Known but not connected - forward to Instr_1 and DELETE
                            int instr1_fd = -1;
                            for (const auto& cs : connection_states) {
                                if (cs.second.is_peer && cs.second.handshake_complete && 
                                    cs.second.peer_name == "Instr_1") {
                                    // Check if Instr_1 is already in hops (loop prevention)
                                    if (!isInHops("Instr_1", forward_hops)) {
                                        instr1_fd = cs.first;
                                    }
                                    break;
                                }
                            }
                            
                            // Don't send back to origin
                            if (instr1_fd != -1 && instr1_fd != sockfd) {
                                std::string hop_str = buildHopString(forward_hops);
                                std::string forward_msg = "SENDMSG," + to_group + "," + from_group + "," + message + static_cast<char>(EOT) + hop_str;
                                bool sent_ok = Networking::sendMessage(instr1_fd, forward_msg);
                                if (sent_ok) {
                                    log("TIER 2: Destination " + to_group + " known but not connected - forwarded to Instr_1 with hops - DELETING from queue");
                                    // Delete from queue (delegated to Instr_1)
                                    group_messages[to_group].pop_back();
                                } else {
                                    log("TIER 2: Failed to send SENDMSG to Instr_1 fd=" + std::to_string(instr1_fd) + " - keeping in queue");
                                }
                            } else if (instr1_fd == sockfd) {
                                // Message came FROM Instr_1, so keep it
                                log("TIER 2: Message from Instr_1 for known destination " + to_group + " - KEEPING in queue (can't forward back to Instr_1)");
                            } else {
                                log("TIER 2: Destination " + to_group + " known but Instr_1 not connected or in hop list - KEEPING in queue");
                            }
                        } else {
                            // Tier 3: Unknown destination - keep in queue
                            log("TIER 3: Destination " + to_group + " unknown - KEEPING in queue for GETMSGS retrieval");
                        }
                    }
                } else {
                    log("Message is for us (" + group_id + ") - stored and not forwarding");
                }
            } else {
                // Duplicate message - aggregate counts and rate-limit logging to avoid flood
                auto now = std::chrono::steady_clock::now();
                duplicate_counts[signature]++;
                bool should_log = false;
                if (duplicate_last_log.find(signature) == duplicate_last_log.end()) {
                    should_log = true; // first duplicate occurrence, log it
                } else {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - duplicate_last_log[signature]).count();
                    if (elapsed >= 10) { // log at most once every 10 seconds per signature
                        should_log = true;
                    }
                }

                if (should_log) {
                    std::string hop_info = "";
                    if (!hops.empty()) {
                        hop_info = " [hops: " + buildHopString(hops) + "]";
                    }
                    log("Duplicate SENDMSG from peer IGNORED (recent): " + signature + hop_info + " (count=" + std::to_string(duplicate_counts[signature]) + ")");
                    duplicate_last_log[signature] = now;
                }

                return ""; // Duplicate message - ignore
            }
            return ""; // No response needed for SENDMSG
        }
        else if (command.rfind("KEEPALIVE,", 0) == 0) {
            std::string msg_count_str = command.substr(10);
            try {
                int msg_count = std::stoi(msg_count_str);
                log("Received KEEPALIVE from peer " + connection_states[sockfd].peer_name + " with " + std::to_string(msg_count) + " messages waiting");
                
                // Request messages for all known groups (not just ours)
                // This allows us to act as a message hub, collecting and storing messages for any group
                if (msg_count > 0) {
                    // First, try to get messages for our own group
                    std::string getmsgs_cmd = "GETMSGS," + group_id;
                    Networking::sendMessage(sockfd, getmsgs_cmd);
                    log("Sent GETMSGS to peer " + connection_states[sockfd].peer_name + " for our group: " + group_id);
                    
                    // Then, request messages for all other known groups
                    std::set<std::string> requested;
                    requested.insert(group_id);  // Mark ours as already requested
                    
                    for (const auto& entry : known_servers) {
                        const std::string& peer_name = entry.second.name;
                        if (requested.find(peer_name) == requested.end()) {
                            std::string cmd = "GETMSGS," + peer_name;
                            Networking::sendMessage(sockfd, cmd);
                            log("Sent GETMSGS to peer " + connection_states[sockfd].peer_name + " for group: " + peer_name);
                            requested.insert(peer_name);
                        }
                    }
                }
            } catch (...) {
                log("Received malformed KEEPALIVE: " + command);
            }
            return "";
        }
        else if (command.rfind("GETMSGS,", 0) == 0) {
            std::string target_group = command.substr(8);
            log("Received GETMSGS for group: " + target_group);
            
            // Check if we have messages for the requested group
            if (group_messages.find(target_group) != group_messages.end()) {
                auto &q = group_messages[target_group];
                if (!q.empty()) {
                    // Pop a message and send it as a SENDMSG frame back to the requesting peer
                    StoredMessage msg = q.front();
                    q.pop_front();
                    
                    // Include hops in the SENDMSG response
                    std::string hop_str = buildHopString(msg.hops);
                    std::string sendcmd = "SENDMSG," + target_group + "," + msg.from_group + "," + msg.content;
                    if (!hop_str.empty()) {
                        sendcmd += static_cast<char>(EOT) + hop_str;
                    }
                    
                    Networking::sendMessage(sockfd, sendcmd);
                    // Log without hops for cleaner output
                    std::string log_msg = "SENDMSG," + target_group + "," + msg.from_group + "," + msg.content;
                    if (!hop_str.empty()) {
                        log_msg += " [hops: " + hop_str + "]";
                    }
                    log("Sent SENDMSG to peer requesting messages: " + log_msg + " (for group " + target_group + ")");
                    return ""; // we've already sent the response
                }
            }
            
            return "NO_MESSAGES";
        }
        else if (command == "STATUSREQ") {
            // Build STATUSRESP with all known servers (ours first) and their message counts
            // Format: STATUSRESP,ourgroup,0,peer1,count1,peer2,count2,...
            std::string response = "STATUSRESP," + group_id + ",0";
            
            // Add all connected peers with their message counts
            std::set<std::string> seen_peers;
            seen_peers.insert(group_id);  // Mark ourselves as seen
            
            for (const auto& cs : connection_states) {
                if (cs.second.is_peer && cs.second.handshake_complete && !cs.second.peer_name.empty()) {
                    if (seen_peers.find(cs.second.peer_name) == seen_peers.end()) {
                        // Count messages for this peer
                        size_t msg_count = 0;
                        if (group_messages.find(cs.second.peer_name) != group_messages.end()) {
                            msg_count = group_messages[cs.second.peer_name].size();
                        }
                        response += "," + cs.second.peer_name + "," + std::to_string(msg_count);
                        seen_peers.insert(cs.second.peer_name);
                    }
                }
            }
            
            // Add all known servers not yet included
            for (const auto& entry : known_servers) {
                const std::string& peer_name = entry.second.name;
                if (seen_peers.find(peer_name) == seen_peers.end()) {
                    // Count messages for this peer
                    size_t msg_count = 0;
                    if (group_messages.find(peer_name) != group_messages.end()) {
                        msg_count = group_messages[peer_name].size();
                    }
                    response += "," + peer_name + "," + std::to_string(msg_count);
                    seen_peers.insert(peer_name);
                }
            }

            return response;
        }
        else if (command.rfind("STATUSRESP,", 0) == 0) {
            log("Received STATUSRESP: " + command);

            // Parse STATUSRESP which is formatted as: STATUSRESP,<group1>,<count1>,<group2>,<count2>,...
            // We will scan pairs and, if any pair reports messages for our group_id, request them.
            // Split payload after the "STATUSRESP," prefix
            std::string payload = command.substr(strlen("STATUSRESP,"));
            std::vector<std::string> parts;
            size_t pos = 0;
            while (pos < payload.size()) {
                size_t comma = payload.find(',', pos);
                if (comma == std::string::npos) {
                    parts.push_back(payload.substr(pos));
                    break;
                }
                parts.push_back(payload.substr(pos, comma - pos));
                pos = comma + 1;
            }
            
            // Scan pairs for messages addressed to us
            for (size_t i = 0; i + 1 < parts.size(); i += 2) {
                const std::string &reported_group = parts[i];
                const std::string &count_str = parts[i+1];
                
                try {
                    int cnt = std::stoi(count_str);
                    if (cnt > 0 && reported_group == group_id) {
                        // Peer reports they have messages for us - request them
                        std::string getcmd = "GETMSGS," + group_id;
                        Networking::sendMessage(sockfd, getcmd);
                        log("Sent GETMSGS to peer " + connection_states[sockfd].peer_name + 
                            " - they have " + std::to_string(cnt) + " messages for us");
                        break;  // Only request once per STATUSRESP
                    }
                } catch (...) {
                    // Continue with next pair
                }
            }

            return "";
        }
        else if (command.rfind("SERVERS,", 0) == 0) {
            if (sockfd >= 0) {
                std::string servers_list = command.substr(8);

                // Parse server entries robustly: expect entries of the form
                // <group>,<ip>,<port>;
                std::vector<std::tuple<std::string,std::string,int>> parsed_servers;
                size_t start = 0;
                while (start < servers_list.length()) {
                    size_t semicolon = servers_list.find(';', start);
                    if (semicolon == std::string::npos) break;

                    std::string entry = servers_list.substr(start, semicolon - start);

                    // Split entry on commas into exactly 3 parts
                    std::vector<std::string> parts;
                    size_t a = 0;
                    while (a < entry.size()) {
                        size_t b = entry.find(',', a);
                        if (b == std::string::npos) { parts.push_back(entry.substr(a)); break; }
                        parts.push_back(entry.substr(a, b - a));
                        a = b + 1;
                    }

                    if (parts.size() == 3) {
                        std::string peer_group = normalizeName(parts[0]);
                        std::string peer_ip = parts[1];
                        std::string peer_port_str = parts[2];

                        // Validate IP and port
                        in_addr addr;
                        bool ip_ok = (inet_pton(AF_INET, peer_ip.c_str(), &addr) == 1);
                        int peer_port = -1;
                        try {
                            peer_port = std::stoi(peer_port_str);
                        } catch (...) { peer_port = -1; }

                        if (!ip_ok) {
                            log("Skipping SERVERS entry with invalid IP: '" + peer_ip + "'");
                        } else if (peer_port < 1 || peer_port > 65535) {
                            log("Skipping SERVERS entry with invalid port: '" + peer_port_str + "'");
                        } else {
                            parsed_servers.emplace_back(peer_group, peer_ip, peer_port);
                        }
                    } else {
                        log("Skipping malformed SERVERS entry: '" + entry + "'");
                    }

                    start = semicolon + 1;
                }

                // Update connection state for this peer
                // The first entry in a peer's SERVERS message typically refers to itself
                if (connection_states.find(sockfd) != connection_states.end() && !parsed_servers.empty()) {
                    ConnectionState& state = connection_states[sockfd];
                    
                    // Get the first entry (usually the peer itself)
                    const std::string &first_peer_group = std::get<0>(parsed_servers[0]);
                    const std::string &first_peer_ip = std::get<1>(parsed_servers[0]);
                    int first_peer_port = std::get<2>(parsed_servers[0]);
                    
                    // Check if the first entry matches this peer's IP and port
                    bool ip_matches = (first_peer_ip == state.peer_ip);
                    bool port_matches = (state.peer_port == 0 || first_peer_port == state.peer_port);
                    
                    if (ip_matches && port_matches) {
                        // This entry refers to the peer we're connected to
                        if (!state.peer_name.empty() && first_peer_group != state.peer_name) {
                            log("Peer announced '" + state.peer_name + "' in HELO but '" + first_peer_group + 
                                "' in SERVERS - updating to SERVERS name");
                        }
                        state.peer_name = first_peer_group;
                        if (state.peer_port == 0) {
                            state.peer_port = first_peer_port;
                        }
                        addKnownServer(state.peer_name, first_peer_ip, first_peer_port);
                    }
                }

                // --- dedupe parsed_servers by ip:port ---
                {
                    std::set<std::string> seen_keys;
                    std::vector<std::tuple<std::string,std::string,int>> unique_parsed;
                    for (const auto &t : parsed_servers) {
                        std::string key = std::get<1>(t) + ":" + std::to_string(std::get<2>(t));
                        if (seen_keys.insert(key).second) {
                            unique_parsed.push_back(t);
                        }
                    }
                    parsed_servers.swap(unique_parsed);
                }

                // Update candidate peers from this SERVERS message
                for (const auto &t : parsed_servers) {
                    const std::string &peer_group = std::get<0>(t);
                    const std::string &peer_ip = std::get<1>(t);
                    int peer_port = std::get<2>(t);
                    if (peer_group != group_id) {
                        addKnownServer(peer_group, peer_ip, peer_port);
                    }
                }

                // Auto-connect to new servers (prefer student servers named A5_*, limit to 8 peers)
                std::vector<std::tuple<std::string, std::string, int>> student_peers;
                std::vector<std::tuple<std::string, std::string, int>> instructor_peers;

                for (const auto &t : parsed_servers) {
                    const std::string &peer_group = std::get<0>(t);
                    const std::string &peer_ip = std::get<1>(t);
                    int peer_port = std::get<2>(t);
                    if (peer_group == group_id) continue;
                    // Simple heuristic: student groups start with "A5_"
                    if (peer_group.rfind("A5_", 0) == 0) {
                        student_peers.emplace_back(peer_group, peer_ip, peer_port);
                    } else {
                        instructor_peers.emplace_back(peer_group, peer_ip, peer_port);
                    }
                }

                int current_peer_count = 0;
                for (const auto &cs : connection_states) {
                    if (cs.second.is_peer && cs.second.handshake_complete) {
                        current_peer_count++;
                    }
                }

                auto try_connect_list = [&](const std::vector<std::tuple<std::string,std::string,int>>& list) {
                    for (const auto &s : list) {
                        if (current_peer_count >= 8) break;
                        const std::string &peer_group = std::get<0>(s);
                        const std::string &peer_ip = std::get<1>(s);
                        int peer_port = std::get<2>(s);

                        // Check if already connected
                        bool already_connected = false;
                        for (const auto &cs : connection_states) {
                            if (cs.second.is_peer && cs.second.peer_name == peer_group) {
                                already_connected = true;
                                break;
                            }
                        }

                        if (!already_connected) {
                            log("Auto-connecting to peer " + peer_group + " at " + peer_ip + ":" + std::to_string(peer_port));
                            initiatePeerConnection(peer_ip, peer_port, fds, -1, peer_group);
                            current_peer_count++;
                        }
                    }
                };

                // Connect to student peers first, then instructor peers
                try_connect_list(student_peers);
                // Also try connecting to instructor peers (up to the 8 peer limit)
                try_connect_list(instructor_peers);
            }
            return "";
        }
        else if (command.rfind("ERROR,", 0) == 0) {
            log("Received ERROR from peer: " + command);
            return "";
        }
        
        log("Unknown server command: " + command);
        return "";
    }
    
    // Close a connection
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
        
        // Remove from pending connections if present
        for (size_t i = 0; i < pending_connections.size(); i++) {
            if (pending_connections[i].sockfd == sockfd) {
                pending_connections.erase(pending_connections.begin() + i);
                break;
            }
        }
    }
    
public:
    TSAMServer(const std::string& id, int port) : running(false), server_port(port), group_id(id) {
        logfile.open("server2.log", std::ios::app);
        if (!logfile.is_open()) {
            std::cerr << "Warning: failed to open server2.log for writing\n";
        }
    }
    
    void start() {
        running = true;
        
        try {
            int server_fd = Networking::createServerSocket(server_port);
            log("Server " + group_id + " listening on port " + std::to_string(server_port));
            
            // Don't set server socket to non-blocking - accept() should block
            // setNonBlocking(server_fd);
            
            // Poll setup
            std::vector<pollfd> fds;
            pollfd server_pollfd;
            server_pollfd.fd = server_fd;
            server_pollfd.events = POLLIN;
            fds.push_back(server_pollfd);
            
            // Main event loop
            while (running) {
                int poll_count = poll(fds.data(), fds.size(), 100); // 100ms timeout
                
                if (poll_count < 0) {
                    if (running) {
                        log("Poll error");
                    }
                    break;
                }
                
                // Perform periodic maintenance regardless of poll activity
                auto now = std::chrono::steady_clock::now();
                
                // Check if it's time to maintain peer connections (every 60 seconds)
                int elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - last_server_check).count();
                if (elapsed_sec >= SERVER_CHECK_INTERVAL_SEC) {
                    last_server_check = now;
                    maintainPeerConnections(fds);
                }
                
                // Send KEEPALIVE and STATUSREQ messages to all connected peers (at most once per minute)
                // Also periodically re-send HELO to verify peer is still alive
                for (auto& cs_pair : connection_states) {
                    if (cs_pair.second.is_peer && cs_pair.second.handshake_complete) {
                        auto elapsed_since_keepalive = std::chrono::duration_cast<std::chrono::seconds>(now - cs_pair.second.last_keepalive_sent).count();
                        if (elapsed_since_keepalive >= 150) {  // Send KEEPALIVE and STATUSREQ at most once per two and a half minutes
                            // Periodically re-send HELO to verify peer is still responsive
                            std::string helo_msg = "HELO," + group_id;
                            Networking::sendMessage(cs_pair.first, helo_msg);
                            log("Sent HELO ping to peer " + cs_pair.second.peer_name + " (alive check)");
                            
                            // Count messages waiting for this peer
                            int msg_count = 0;
                            for (const auto& msg_group : group_messages) {
                                msg_count += msg_group.second.size();
                            }
                            
                            std::string keepalive_msg = "KEEPALIVE," + std::to_string(msg_count);
                            Networking::sendMessage(cs_pair.first, keepalive_msg);
                            cs_pair.second.last_keepalive_sent = now;
                            log("Sent KEEPALIVE to peer " + cs_pair.second.peer_name + " (" + std::to_string(msg_count) + " messages)");
                            
                            // Also send STATUSREQ to check if peer has messages for us
                            Networking::sendMessage(cs_pair.first, "STATUSREQ");
                            log("Sent STATUSREQ to peer " + cs_pair.second.peer_name);
                        }
                    }
                }
                
                // Check for timed-out handshakes (60 seconds timeout)
                std::vector<int> timed_out_fds;
                for (const auto& cs_pair : connection_states) {
                    if (cs_pair.second.is_peer && !cs_pair.second.handshake_complete) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cs_pair.second.connection_start).count();
                        if (elapsed > 60) {
                            log("Handshake timeout for fd=" + std::to_string(cs_pair.first) + " after " + std::to_string(elapsed) + " seconds");
                            timed_out_fds.push_back(cs_pair.first);
                        }
                    }
                }
                
                // Close timed-out connections and notify requesters
                for (int fd : timed_out_fds) {
                    const auto& state = connection_states[fd];
                    if (state.requester_fd != -1) {
                        if (connection_states.find(state.requester_fd) != connection_states.end()) {
                            std::string msg = "ERROR,CONNECTION_TIMEOUT to " + state.peer_ip + ":" + std::to_string(state.peer_port);
                            Networking::sendMessage(state.requester_fd, msg);
                            log("Notified requester fd=" + std::to_string(state.requester_fd) + " of timeout");
                        }
                    }
                    closeConnection(fd, fds);
                }
                
                // Skip processing events if poll timed out with no activity
                if (poll_count == 0) {
                    continue;
                }
                
                // Process all file descriptors
                for (size_t i = 0; i < fds.size(); i++) {
                    if (fds[i].revents == 0) continue;
                    
                    int fd = fds[i].fd;
                    
                    // Check for errors first
                    if (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                        if (fd != server_fd) {
                            log("Connection error/hangup on fd=" + std::to_string(fd));
                            
                            // If this is a pending outgoing connection, notify the requester
                            for (auto it = pending_connections.begin(); it != pending_connections.end(); ++it) {
                                if (it->sockfd == fd) {
                                    if (it->requester_fd != -1) {
                                        if (connection_states.find(it->requester_fd) != connection_states.end()) {
                                            std::string msg = "ERROR,CONNECTION_FAILED to " + it->ip + ":" + std::to_string(it->port);
                                            Networking::sendMessage(it->requester_fd, msg);
                                        }
                                    }
                                    break;
                                }
                            }
                            
                            closeConnection(fd, fds);
                            i--; // Adjust index after removal
                        }
                        continue;
                    }
                    
                    // Server socket - new connection
                    if (fd == server_fd && (fds[i].revents & POLLIN)) {
                        sockaddr_in connection_addr;
                        socklen_t connection_len = sizeof(connection_addr);
                        int client_socket = accept(server_fd, (sockaddr*)&connection_addr, &connection_len);

                        if (client_socket >= 0) {
                            char peer_ipbuf[INET_ADDRSTRLEN];
                            inet_ntop(AF_INET, &connection_addr.sin_addr, peer_ipbuf, sizeof(peer_ipbuf));
                            int peer_port = ntohs(connection_addr.sin_port);
                            std::string peer_ip = std::string(peer_ipbuf);
                            
                            log("New connection accepted from " + peer_ip + ":" + std::to_string(peer_port) + " on fd=" + std::to_string(client_socket));
                            
                            // CHECK IP RATE LIMIT - reject if too many rapid connections from same IP
                            if (isIPRateLimited(peer_ip)) {
                                log("REJECTING connection from " + peer_ip + ":" + std::to_string(peer_port) + " - IP rate limited (too many rapid connections)");
                                close(client_socket);
                                continue;
                            }
                            
                            // Record this connection attempt
                            recordIPConnectionAttempt(peer_ip);
                            
                            // Set non-blocking to prevent recv() from blocking the poll loop
                            setNonBlocking(client_socket);
                            
                            // Add to poll
                            pollfd new_fd;
                            new_fd.fd = client_socket;
                            new_fd.events = POLLIN;
                            fds.push_back(new_fd);
                            
                            // Initialize connection state
                            ConnectionState state;
                            // Record the remote IP that connected (source IP). The peer's listening
                            // port is unknown at this point, so keep peer_port==0 until updated
                            state.peer_ip = peer_ip;
                            state.peer_port = 0;
                            connection_states[client_socket] = state;
                            
                            log("Added fd=" + std::to_string(client_socket) + " to poll list");
                        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            log("Accept error: " + std::string(strerror(errno)));
                        }
                    }
                    // Outgoing connection ready or in PROGRESS
                    else if (fds[i].revents & POLLOUT) {
                        // Find in pending connections / IN PROGRESS
                        for (auto it = pending_connections.begin(); it != pending_connections.end(); ++it) {
                            if (it->sockfd == fd) {
                                // Check if connection succeeded
                                int error = 0;
                                socklen_t len = sizeof(error);
                                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                                        // TCP connection succeeded - proceed with handshake
                                        // Pass requester_fd to connection state, will notify after HELO completes
                                        handleNewPeerConnection(fd, it->ip, it->port, fds, it->expected_peer_name, it->requester_fd);
                                        pending_connections.erase(it);
                                } else {
                                    log("Connection to " + it->ip + ":" + std::to_string(it->port) + " failed");
                                    
                                    // Notify requesting client of failure
                                    if (it->requester_fd != -1) {
                                        if (connection_states.find(it->requester_fd) != connection_states.end()) {
                                            std::string msg = "ERROR,CONNECTION_FAILED to " + it->ip + ":" + std::to_string(it->port);
                                            Networking::sendMessage(it->requester_fd, msg);
                                        }
                                    }
                                    
                                    closeConnection(fd, fds);
                                }
                                break;
                            }
                        }
                    }
                    // Data available on existing connection
                    else if (fds[i].revents & POLLIN) {
                        std::string message = Networking::receiveMessage(fd);
                        
                        // Check for frame corruption signal
                        if (message == "__FRAME_CORRUPTION__") {
                            log("Closing connection fd=" + std::to_string(fd) + " due to frame corruption");
                            closeConnection(fd, fds);
                        }
                        else if (!message.empty()) {
                            // Process the message
                            processMessage(fd, message, fds);
                        }
                        // If message is empty, it could be:
                        // - EAGAIN (no complete message yet) - keep connection
                        // - Connection will be closed by POLLHUP event later if actually disconnected
                        // So we don't close here, just skip processing
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
        if (logfile.is_open()) {
            logfile.close();
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }
    
    int port = std::stoi(argv[1]);
    std::string group_id = "A5_29";
    
    signal(SIGPIPE, SIG_IGN);
    
    TSAMServer server(group_id, port);
    
    std::cout << "Starting TSAM Server " << group_id << " on port " << port << std::endl;
    std::cout << "Press Ctrl+C to stop..." << std::endl;
    
    signal(SIGINT, [](int) { /* Handler */ });
    
    server.start();
    
    return 0;
}
