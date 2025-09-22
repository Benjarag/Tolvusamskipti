#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <iostream>
#include <string>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>


void check_if_digit(std::string str) {
    for (char c : str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            std::cerr << "non-numeric character in port\n";
            exit(0);
        }
    }
}
void check_in_range(int str) {
    if (str < 1 || str > 65535) {
        std::cerr << "port is out of range\n";
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    /*
    *   UDP port scanner, that takes in as arguments the IP address of the machine,
    *   and a range of ports to scan between.
    *    
    *   The scanner should be run with the command:
    *   ./scanner <IP address> <low port> <high port>        
    *   
    *   SOCK_DGRAM = UDP
    *   AF_INET = IP
    */
    int high_port = 0;
    int low_port = 0;

    // check for right number of arguments
    if (argc != 4) {
        std::cerr << "command needs to be ./scanner [IP ADDRESS] [LOW PORT] [HIGH PORT]\n";
        exit(0);
    }
    
    // IP ADDRESS is argument nr 1
    const char* ip_string = argv[1];

    // LOW PORT is argument nr 2
    // HIGH PORT is argument nr 3
    std::string low_port_str = argv[2];
    std::string high_port_str = argv[3];

    // Make sure both ports are just digits
    check_if_digit(low_port_str);
    check_if_digit(high_port_str);

     // Turn ports into int
    int low_parsed = std::stoi(low_port_str);
    int high_parsed = std::stoi(high_port_str);

    // Make sure they are in range (1 - 65535)
    check_in_range(low_parsed);
    check_in_range(high_parsed);

    // Ports have been validated
    high_port = high_parsed;
    low_port = low_parsed;

    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(0);
    }
    
    // The destination address structure
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(ip_string);

    // Set timeout for receiving responses to 1 second
    struct timeval tv;
    tv.tv_sec = 1; // 1 second
    tv.tv_usec = 0; 
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt failed");
        close(sock);
        exit(0);
    }

    // Buffer for sending and receiving data
    char send_buffer[1] = {0};
    char recv_buffer[1024];

    // Scan ports in the given range
    std::cout << "Scanning ports " << low_port << " to " << high_port << " on " << ip_string << "...\n";
    for (int port = low_port; port <= high_port; ++port) {
        dest_addr.sin_port = htons(port); // Set the port for this iteration

        // Send UDP packet
        if (sendto(sock, send_buffer, sizeof(send_buffer), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr)) < 0) {
            perror("sendto failed");
            continue;
        }

        // Try to receive a response
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int recv_len = recvfrom(sock, recv_buffer, sizeof(recv_buffer), 0, (struct sockaddr*)&from_addr, &from_len);
        if (recv_len >= 0) {
            // If we get a response, the port is open
            std::cout << "Port " << port << " is open\n";
        }
        else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // An error occurred other than timeout
            perror("recvfrom failed");
        }
        // If timeout occurs, we assume the port is closed or filtered and do nothing
    }

    close(sock);
    return 0;
}