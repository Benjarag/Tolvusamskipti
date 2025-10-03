#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    int sock, portno;
    struct sockaddr_in serv_addr;
    char buffer[25];
    
    // get port number from command line arguments
    if (argc == 2) {
        portno = atoi(argv[1]);
    } else {
        portno = 4000;
    }
    
    // create socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("ERROR opening socket:");
        exit(1);
    }
    
    // set own address and bind socket
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    
    if (bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR binding to socket:");
        exit(1);
    }
    
    // receive messages
    int i = 1, n = 1;
    while (n > 0) {
        n = recv(sock, buffer, sizeof(buffer), 0);
        if (n < 0) {
            perror("ERROR receiving:");
            exit(0);
        } else if (n > 0) {
            std::cout << "msg " << i << ": " << std::string(buffer, n) << std::endl;
        }
        i++;
    }
    
    close(sock);
    return 0;
}