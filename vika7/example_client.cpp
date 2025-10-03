#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <vector>
#include <string>

int main(int argc, char* argv[]) {
    int sock, portno;
    struct sockaddr_in serv_addr;
    char* ip = nullptr;

    // read server ip and port from command line arguments
    if (argc == 3) {
        ip = argv[1];
        portno = std::atoi(argv[2]);
    } else {
        std::cout << "usage: " << argv[0] << " IP PORT" << std::endl;
        return 1;
    }

    // create socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::perror("ERROR opening socket");
        return 1;
    }

    // set server address
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) != 1) {
        std::perror("invalid ip");
        close(sock);
        return 1;
    }

    // connect to server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::perror("ERROR connecting");
        close(sock);
        return 1;
    }

    // set up list of messages
    std::vector<std::string> messages;
    messages.push_back("Hi");
    messages.push_back("Hi again");
    messages.push_back("Am I talking to myself here?");
    messages.push_back("Why are you not answering me?");
    messages.push_back("");
    messages.push_back("Bye.");

    // send all messages
    int i = 1;
    for (const auto& msg : messages) {
        if (send(sock, msg.c_str(), msg.length(), 0) < 0) {
            std::perror("ERROR sending");
            close(sock);
            return 1;
        } else {
            std::cout << "msg " << i << ": " << msg << std::endl;
        }
        ++i;
    }

    close(sock);
    return 0;
}