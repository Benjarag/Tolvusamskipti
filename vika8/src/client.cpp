#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include "protocol.h"

class TSAMClient
{
private:
    bool connected;
    
public:
    TSAMClient() : connected(false) {}
    
    bool connectToServer(const std::string& host, int port) {
        std::cout << "Would connect to " << host << ":" << port << std::endl;
        // TODO: Add socket connection code here
        connected = true;
        return connected;
    }
    
    void sendCommand(const std::string& command) {
        if (!connected) {
            std::cout << "Not connected to server" << std::endl;
            return;
        }
        std::cout << "Would send: " << command << std::endl;
        // TODO: Add protocol encoding and socket send here
    }
    
    void disconnect() {
        connected = false;
        std::cout << "Disconnected" << std::endl;
    }
};

void showHelp() {
    std::cout << "\n=== TSAM Client Commands ===" << std::endl;
    std::cout << "CONNECT,<ip>,<port>  Connect to server" << std::endl;
    std::cout << "SENDMSG,<group>,<msg> Send message" << std::endl;
    std::cout << "GETMSG               Get messages" << std::endl;
    std::cout << "LISTSERVERS          List servers" << std::endl;
    std::cout << "DISCONNECT           Disconnect" << std::endl;
    std::cout << "HELP                 Show help" << std::endl;
    std::cout << "QUIT                 Exit" << std::endl;
    std::cout << "=============================" << std::endl;
}

int main() {
    std::cout << "TSAM Client starting (Group 29) ..." << std::endl;
    showHelp();

    TSAMClient client;
    std::string input;

    while (true) {
        std::cout << "tsamgroup29> ";
        std::getline(std::cin, input);

        // TODO: Add command handling here
        if (input == "QUIT") {
            std::cout << "Exiting TSAM Client." << std::endl;
            break;
        } else if (input == "HELP") {
            showHelp();
        } else if (input == "GETMSG") {
            // if (client.isConnected()) {
            //     client.SendCommand("GETMSG");
            // } else {
            //     std::cout << "Not connected to any server. Use CONNECT command first." << std::endl;
            // }
            break;
        } else if (input == "LISTSERVERS") {
            break;
        } else if (input == "SENDMSG") {
            break;
        } else if (input == "CONNECT") {
            break;
        } else if (input == "DISCONNECT") {
            break;
        } else if (input == "TESTPROTOCOL") {
            // testProtocol();
            break;
        } else {
            std::cout << "Unknown command. Type HELP for a list of commands." << std::endl;
        }
    }


}