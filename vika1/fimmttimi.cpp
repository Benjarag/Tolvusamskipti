#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <unistd.h>

int main(int argc, char* argv[]) {
    
    int sock;
    // TODO: read port from command line arguments
    int port = 4000;
    // TODO: check for right number of arguments
    char *ip_string = argv[1];

    if (sock = socket(AF_INET, SOCK_STREAM, 0) < 0) {
        perror("socket creation failed");
        exit(0);
    }
    
    // construct destination address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_string, &server_addr.sin_addr) != 1) {
        std::cout << "ip address is weird" << std::endl;
        exit(0);
    }

    // connect to server
    if (connect(sock, (const sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        exit(0);
    }

    std::string message = "Hi! How are you?";

    int n = send(sock, message.c_str(), message.length(), 0);
    if (n < message.length()) {
        std::cout << "did not send everything" << std::endl;
    }

    if (close(sock) < 0) {
        perror("close failed");
    }

    return 0;
}