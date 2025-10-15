#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "networking.h"
#include "protocol.h"
#include <unistd.h>

class TSAMClient {
private:
    std::atomic<bool> connected;
    int server_socket;
    
    void log(const std::string& message) {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::cout << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << message << std::endl;
    }
    
public:
    TSAMClient() : connected(false), server_socket(-1) {}
    
    bool connectToServer(const std::string& host, int port) {
        try {
            server_socket = Networking::createClientSocket(host, port);
            connected = true;
            log("Connected to server " + host + ":" + std::to_string(port));
            return true;
        } catch (const std::exception& e) {
            log("Connection failed: " + std::string(e.what()));
            return false;
        }
    }
    
    void sendCommand(const std::string& command) {
        if (!connected) {
            log("Not connected to server");
            return;
        }
        
        log("Sending: " + command);
        
        if (Networking::sendMessage(server_socket, command)) {
            std::string response = Networking::receiveMessage(server_socket); // receuving message from server
            if (!response.empty()) {
                log("Server response: " + response);
            } else {
                log("No response from server or connection closed");
                connected = false;
            }
        } else {
            log("Failed to send command");
            connected = false;
        }
    }
    
    void disconnect() {
        if (connected) {
            close(server_socket);
            connected = false;
            log("Disconnected from server");
        }
    }
    
    bool isConnected() const {
        return connected;
    }
    
    ~TSAMClient() {
        disconnect();
    }
};

void showHelp() {
    std::cout << "\n=== TSAM Client Commands ===" << std::endl;
    std::cout << "SENDMSG,GROUP_ID,<message>   Send message to group (comma-separated)" << std::endl;
    std::cout << "GETMSG                Get messages for your group" << std::endl;
    std::cout << "LISTSERVERS           List connected servers" << std::endl;
    std::cout << "CONNECT <ip> <port>   Tell server to connect to another server" << std::endl;
    std::cout << "HELP                  Show this help" << std::endl;
    std::cout << "QUIT                  Exit client" << std::endl;
    std::cout << "=============================" << std::endl;
}

std::vector<std::string> splitCommand(const std::string& input) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream stream(input);
    
    while (stream >> token) {
        tokens.push_back(token);
    }
    
    return tokens;
}

int main(int argc, char* argv[]) {
    // Auto-connect behavior: host and port can be provided as optional args.
    // Usage: ./tsamclient [host port]
    std::string host = "127.0.0.1";
    int port = 12345;
    if (argc == 3) {
        host = argv[1];
        port = std::stoi(argv[2]);
    } else if (argc != 1) {
        std::cout << "Usage: " << argv[0] << " [host port]" << std::endl;
        return 1;
    }

    std::cout << "TSAM Client starting (Group 29) ..." << std::endl;
    TSAMClient client;

    // Attempt to connect automatically on startup. Exit if connection fails.
    if (!client.connectToServer(host, port)) {
        std::cerr << "Failed to connect to server at " << host << ":" << port << ". Exiting." << std::endl;
        return 1;
    }

    showHelp();

    std::string input;

    while (true) {
        std::cout << "tsamgroup29> ";
        std::getline(std::cin, input);
        
        if (input.empty()) continue;

        // Only accept the comma-separated SENDMSG form: SENDMSG,GROUP_ID,<message>
        // Message may include commas and spaces; send the payload verbatim.
        if (input.size() >= 8) {
            std::string prefix = input.substr(0, 8);
            for (auto &c : prefix) c = std::toupper((unsigned char)c);
            if (prefix == "SENDMSG,") {
                // Normalize the command prefix to uppercase and preserve the rest
                std::string to_send = std::string("SENDMSG,") + input.substr(8);
                client.sendCommand(to_send);
                continue;
            }
        }

        auto tokens = splitCommand(input);
        std::string command = tokens[0];

        if (command == "QUIT") {
            client.disconnect();
            std::cout << "Exiting TSAM Client." << std::endl;
            break;
        } 
        else if (command == "HELP") {
            showHelp();
        }
        else if (command == "GETMSG") {
            client.sendCommand("GETMSG");
        }
        else if (command == "LISTSERVERS") {
            client.sendCommand("LISTSERVERS");
        }
        else if (command == "CONNECT") {
            if (tokens.size() < 3) {
                std::cout << "Usage: CONNECT <ip> <port>" << std::endl;
            } else {
                std::string connect_cmd = "CONNECT " + tokens[1] + " " + tokens[2];
                client.sendCommand(connect_cmd);
            }
        }
        else {
            std::cout << "Unknown command. Type HELP for a list of commands." << std::endl;
        }
    }

    return 0;
}