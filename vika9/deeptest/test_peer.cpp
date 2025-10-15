// Simple tool to test server-to-server communication
#include <iostream>
#include <string>
#include "networking.h"
#include "protocol.h"
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <server_port>" << std::endl;
        std::cerr << "Example: " << argv[0] << " 127.0.0.1 4029" << std::endl;
        return 1;
    }

    std::string host = argv[1];
    int port = std::stoi(argv[2]);

    try {
        std::cout << "Connecting to server " << host << ":" << port << " as peer..." << std::endl;
        int sock = Networking::createClientSocket(host, port);
        
        // Send HELO as a peer server
        std::string helo = "HELO,A5_TEST";
        std::cout << "Sending: " << helo << std::endl;
        Networking::sendMessage(sock, helo);
        
        // Receive SERVERS response
        std::string response = Networking::receiveMessage(sock);
        std::cout << "Received: " << response << std::endl;
        
        // Send a test message
        std::string sendmsg = "SENDMSG,A5_29,A5_TEST,Hello from test peer!";
        std::cout << "Sending: " << sendmsg << std::endl;
        Networking::sendMessage(sock, sendmsg);
        
        std::cout << "Waiting for any responses..." << std::endl;
        sleep(1);
        
        // Try to read another message (non-blocking simulation)
        std::string msg = Networking::receiveMessage(sock);
        if (!msg.empty()) {
            std::cout << "Received: " << msg << std::endl;
        } else {
            std::cout << "No further messages (connection may have closed)" << std::endl;
        }
        
        close(sock);
        std::cout << "Test complete." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
