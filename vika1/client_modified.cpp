#include <iostream> // for printing
#include <string> // std::string like python str
#include <cstring> // c style char arrays
#include <sys/types.h>
#include <sys/socket.h> // sockets lets us talk over the network
#include <netinet/in.h> // internet addresses (ipv4)
#include <unistd.h> // functions for closing sockets
#include <arpa/inet.h> // functions for working with IP addresses

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cout << "Usage: client <server_ip> <port>\n";
        return 1;
    }
    const char *server_ip = argv[1];
    int server_port = std::stoi(argv[2]);

    // create a socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // address of the server
    sockaddr_in server_addr{}; // struct to hold the server's address
    server_addr.sin_family = AF_INET; // IPv4
    server_addr.sin_port = htons(server_port); // converting port number to network byte order
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr); // converting IP address from text to binary form

    // connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    std::cout << "Connected to the server. Type 'exit' to quit.\n";

    while (true) {
        // get the command from the user
        std::string command;
        std::cout << "Enter a command: ";
        std::getline(std::cin, command);

        if (send(sock, command.c_str(), command.size(), 0) < 0) {
            perror("Send failed");
            break;
        }

        // receive the output
        char buffer[4096];
        int n = read(sock, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0'; // null-terminate the received data
            std::cout << "Received: " << buffer << std::endl;
        } else {
            perror("Receive failed");
            break; // exit on receive failure
        }
    }

    close(sock);
    return 0;
}
