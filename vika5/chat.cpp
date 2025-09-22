#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <random>
#include <map>

using namespace std;

// ===== DATA STRUCTURES =====
struct PortInfo {
    int port;
    string type; // "secret", "evil", "checksum", "oracle"
    string response;
};

struct PuzzleData {
    uint8_t group_id;
    uint32_t signature;
    int hidden_port;
    map<string, string> secret_phrases; // port_type -> phrase
};

// ===== NETWORK UTILITIES =====
class NetworkUtils {
public:
    static int create_udp_socket(int timeout_sec = 2) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) return -1;
        
        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        
        return sock;
    }
    
    static sockaddr_in create_sockaddr(const string& ip, int port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        return addr;
    }
    
    static string send_receive(int sock, const sockaddr_in& dest_addr, 
                              const string& message) {
        sendto(sock, message.c_str(), message.size(), 0,
               (const sockaddr*)&dest_addr, sizeof(dest_addr));
        
        char buffer[1024];
        sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);
        int bytes = recvfrom(sock, buffer, sizeof(buffer)-1, 0,
                           (sockaddr*)&sender_addr, &sender_len);
        
        if (bytes > 0) {
            buffer[bytes] = '\0';
            return string(buffer);
        }
        return "";
    }
};

// ===== PORT IDENTIFICATION =====
vector<PortInfo> identify_ports(const string& ip, const vector<int>& ports) {
    vector<PortInfo> identified;
    
    for (int port : ports) {
        int sock = NetworkUtils::create_udp_socket(1);
        if (sock < 0) continue;
        
        sockaddr_in addr = NetworkUtils::create_sockaddr(ip, port);
        string response = NetworkUtils::send_receive(sock, addr, "PROBE");
        
        close(sock);
        
        if (response.find("Greetings from S.E.C.R.E.T") != string::npos) {
            identified.push_back({port, "secret", response});
        } else if (response.find("evil") != string::npos) {
            identified.push_back({port, "evil", response});
        } else if (response.find("Send me a 4-byte message") != string::npos) {
            identified.push_back({port, "checksum", response});
        } else if (response.find("E.X.P.S.T.N") != string::npos) {
            identified.push_back({port, "oracle", response});
        }
    }
    
    return identified;
}

// ===== SECRET PORT HANDLER =====
class SecretPortHandler {
private:
    string ip;
    int port;
    
public:
    SecretPortHandler(const string& server_ip, int secret_port) 
        : ip(server_ip), port(secret_port) {}
    
    PuzzleData solve() {
        int sock = NetworkUtils::create_udp_socket();
        sockaddr_in addr = NetworkUtils::create_sockaddr(ip, port);
        
        // Step 1: Generate secret and send initial message
        uint32_t secret = generate_secret();
        string message = build_initial_message(secret);
        
        sendto(sock, message.c_str(), message.size(), 0,
               (sockaddr*)&addr, sizeof(addr));
        
        // Step 2: Receive challenge
        char response[5];
        socklen_t addr_len = sizeof(addr);
        int bytes = recvfrom(sock, response, 5, 0, (sockaddr*)&addr, &addr_len);
        
        if (bytes == 5) {
            uint8_t group_id = response[0];
            uint32_t challenge;
            memcpy(&challenge, response + 1, 4);
            challenge = ntohl(challenge);
            
            // Step 3: Calculate and send signature
            uint32_t signature = challenge ^ secret;
            send_signature(sock, addr, group_id, signature);
            
            // Step 4: Receive hidden port
            char port_response[1024];
            bytes = recvfrom(sock, port_response, sizeof(port_response), 0,
                           (sockaddr*)&addr, &addr_len);
            
            if (bytes > 0) {
                port_response[bytes] = '\0';
                int hidden_port = extract_hidden_port(port_response);
                
                close(sock);
                return {group_id, signature, hidden_port, {}};
            }
        }
        
        close(sock);
        throw runtime_error("Failed to solve secret port");
    }
    
private:
    uint32_t generate_secret() {
        random_device rd;
        return rd();
    }
    
    string build_initial_message(uint32_t secret) {
        string message = "S";
        uint32_t net_secret = htonl(secret);
        message.append((char*)&net_secret, 4);
        message.append("benjaminr23"); // Your usernames here
        return message;
    }
    
    void send_signature(int sock, const sockaddr_in& addr, uint8_t group_id, uint32_t signature) {
        char message[5];
        message[0] = group_id;
        uint32_t net_sig = htonl(signature);
        memcpy(message + 1, &net_sig, 4);
        
        sendto(sock, message, 5, 0, (sockaddr*)&addr, sizeof(addr));
    }
    
    int extract_hidden_port(const string& response) {
        // Extract port number from response using simple parsing
        size_t pos = response.find("port");
        if (pos != string::npos) {
            // Simple extraction logic - you might need to improve this
            for (size_t i = pos; i < response.length(); i++) {
                if (isdigit(response[i])) {
                    return stoi(response.substr(i));
                }
            }
        }
        throw runtime_error("Could not extract hidden port from response");
    }
};

// ===== MAIN CONTROLLER =====
class PuzzleSolver {
private:
    string server_ip;
    map<string, PortInfo> ports;
    PuzzleData data;
    
public:
    PuzzleSolver(const string& ip, const vector<int>& port_list) : server_ip(ip) {
        // Identify all ports
        vector<PortInfo> identified = identify_ports(ip, port_list);
        for (const auto& info : identified) {
            ports[info.type] = info;
        }
        
        if (ports.size() != 4) {
            throw runtime_error("Could not identify all 4 ports");
        }
    }
    
    void solve() {
        cout << "=== Starting Puzzle Solution ===" << endl;
        
        // Step 1: Solve Secret Port
        cout << "1. Solving S.E.C.R.E.T port..." << endl;
        SecretPortHandler secret_handler(server_ip, ports["secret"].port);
        data = secret_handler.solve();
        cout << "   Group ID: " << (int)data.group_id << endl;
        cout << "   Hidden Port: " << data.hidden_port << endl;
        
        // Step 2: Solve Checksum Port (implement next)
        // solve_checksum_port();
        
        // Step 3: Solve Evil Port (implement next) 
        // solve_evil_port();
        
        // Step 4: Query Oracle (implement next)
        // query_oracle();
        
        // Step 5: Port Knocking (implement last)
        // perform_port_knocking();
    }
};

// ===== MAIN FUNCTION =====
int main(int argc, char* argv[]) {
    if (argc != 6) {
        cerr << "Usage: " << argv[0] << " <IP> <port1> <port2> <port3> <port4>" << endl;
        return 1;
    }

    string ip = argv[1];
    vector<int> ports;
    for (int i = 2; i < 6; i++) {
        ports.push_back(atoi(argv[i]));
    }

    try {
        PuzzleSolver solver(ip, ports);
        solver.solve();
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}