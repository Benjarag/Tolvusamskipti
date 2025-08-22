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

    // get the command from the user
    std::string command;
    std::cout << "Enter a command: ";
    std::getline(std::cin, command);

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

    // send the SYS command
    std::string message = "SYS " + command;
    send(sock, message.c_str(), message.size(), 0);

    std::cout << "Sent: " << message << std::endl;
    close(sock);
    return 0;
}
