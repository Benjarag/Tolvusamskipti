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

    // main loop
    while (true) {
        // get the command from the user
        std::string command;
        std::cout << "Enter a command (or 'quit' to exit): ";
        std::getline(std::cin, command);

        if (command == "quit") {
            break;
        }

        std::string message = "SYS " + command;
        if (send(sock, message.c_str(), message.size(), 0) < 0) {
            perror("Send failed");
            break;
        }

        // receive the output or the response from the server
        char buffer[4096];
        memset(buffer, 0, sizeof(buffer)); // clear the buffer
        // here we then use recv to read the server's response
        int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes <= 0) {
            std::cout << "Server closed connection\n";
            break;
        }        

        std::cout << "Output:\n" << buffer << std::endl;
    }

    close(sock);
    return 0;
}
