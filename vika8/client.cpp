#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>


int main() {
    std::cout << "Starting Simple Echo Server..." << std::endl;
    
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind to port
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(4044);  // Using port 4044
    
    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        return 1;
    }
    
    // Listen for connections
    if (listen(server_fd, 5) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        return 1;
    }
    
    std::cout << "Server listening on port 4044..." << std::endl;
    
    while (true) {
        // Accept connection
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        
        if (client_socket < 0) {
            std::cerr << "Accept failed" << std::endl;
            continue;
        }
        
        std::cout << "Client connected!" << std::endl;
        
        // Handle client
        char buffer[1024] = {0};
        while (true) {
            // Read from client
            int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
            if (bytes_read <= 0) {
                break;  // Client disconnected or error
            }
            
            buffer[bytes_read] = '\0';  // Null terminate
            std::cout << "Received: " << buffer << std::endl;
            
            // Echo back
            write(client_socket, buffer, bytes_read);
            std::cout << "Echoed back: " << buffer << std::endl;
        }
        
        close(client_socket);
        std::cout << "Client disconnected" << std::endl;
    }
    
    close(server_fd);
    return 0;
}