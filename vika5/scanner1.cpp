#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

#define TIMEOUT_SEC 1
#define TIMEOUT_USEC 0

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <IP address> <low port> <high port>" << std::endl;
        return 1;
    }

    const char *ip = argv[1];
    int low_port = std::stoi(argv[2]);
    int high_port = std::stoi(argv[3]);

    if (low_port > high_port) {
        std::cerr << "Error: low port should be less than or equal to high port" << std::endl;
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }

    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = TIMEOUT_USEC;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt failed");
        close(sockfd);
        return 1;
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(0);
    servaddr.sin_addr.s_addr = inet_addr(ip);

    if (servaddr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "Invalid IP address" << std::endl;
        close(sockfd);
        return 1;
    }

    const char *message = "hello!";
    int message_len = strlen(message);

    std::vector<int> open_ports;

    for (int port = low_port; port <= high_port; port++) {
        servaddr.sin_port = htons(port);

        ssize_t sent_bytes = sendto(sockfd, message, message_len, 0, (const struct sockaddr *)&servaddr, sizeof(servaddr));
        if (sent_bytes < 0) {
            perror("sendto failed");
            continue;
        }

        char buffer[1024];
        socklen_t len = sizeof(servaddr);
        ssize_t recv_bytes = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&servaddr, &len);
        if (recv_bytes > 0) {
            open_ports.push_back(port);
        }
    }

    close(sockfd);

    std::cout << "Open ports: ";
    for (int port : open_ports) {
        std::cout << port << " ";
    }
    std::cout << std::endl;

    return 0;
}