#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>
#include <string>
#include <unistd.h>

#include <netinet/ip.h>
#include <netinet/udp.h>
#include <vector>


using namespace std;

void evilBit(char* signature_buffer, int sock, sockaddr_in server_addr, int port2) {

    std::cout << "Signature bytes: " << std::endl;
    for (int i = 0; i < 5; i++) {
        printf("%02X ", (unsigned char)signature_buffer[i]);
    }

    int rawsock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (rawsock < 0) { 
        perror("raw socket failed"); exit(1); }
    
    int one = 1;
    if (setsockopt(rawsock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt failed!");
    } 

    // Setup local address so both raw and udp socket use same port numbers
    sockaddr_in local_addr{};
    socklen_t local_len = sizeof(local_addr);
    if (getsockname(sock, (sockaddr*)&local_addr, &local_len) == -1) {
    perror("getsockname failed");
    
    } else {
        char local_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &local_addr.sin_addr, local_ip_str, sizeof(local_ip_str));
        std::cout << "local IP: " << local_ip_str
                << "  local port: " << ntohs(local_addr.sin_port) << std::endl;
    }
 
    char packetHeader[32];

    int pkt_len = sizeof(struct ip) + sizeof(struct udphdr) + 4;


    struct ip *ipHeader = (struct ip *) packetHeader;
    struct udphdr *udpHeader = (struct udphdr *) (packetHeader + sizeof(struct ip));
    char *data = packetHeader + sizeof(struct ip) + sizeof(struct udphdr);

    ipHeader->ip_hl = 5;
    ipHeader->ip_v = 4;
    ipHeader->ip_tos = 0;
    ipHeader->ip_len = htons(pkt_len);
    ipHeader->ip_id = htons(0);
    ipHeader->ip_off = htons(IP_RF);
    ipHeader->ip_ttl = 64;
    ipHeader->ip_p = 17;
    ipHeader->ip_sum = 0;
    ipHeader->ip_dst.s_addr = inet_addr("130.208.246.98");
    ipHeader->ip_src = local_addr.sin_addr;

    uint16_t local_port = ntohs(local_addr.sin_port);
    udpHeader->uh_sport = htons(local_port);
    udpHeader->uh_dport = htons(port2);
    udpHeader->uh_ulen = htons(sizeof(struct udphdr) + sizeof(int));
    udpHeader->uh_sum = 0;
  
    memcpy(data, signature_buffer + 1, sizeof(int));

    std::cout << "\nPacket header: " << std::endl;
    for (int i = 0; i < sizeof(packetHeader); i++) {
        printf("%02X ", (unsigned char)packetHeader[i]);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = ipHeader->ip_dst.s_addr;
    int sent = sendto(rawsock, packetHeader, pkt_len, 0,
        (sockaddr *)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        perror("sendto failed");
        close(rawsock);
        return;
    }
    
    int new_rec_buffer[69];
    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);
    int received = recvfrom(sock, new_rec_buffer, sizeof(new_rec_buffer), 0,
        (sockaddr *)&from_addr, &from_len);
    std::cout << "Received amount for first evil reply: " << received << std::endl;
    if (received < 0) {
        std::cout << "received failed" << std::endl;
    }

    for (int i = 0; i < received; ++i) printf("%02X ", (unsigned char)((uint8_t*)new_rec_buffer)[i]);
}
/*
uint16_t checksumCalc(uint16_t* ipheaderBytes, int numWords){
    uint32_t outcome = 0;
    // If one is carried for most significant bit we wrap around
    for (int i = 0; i < numWords; i++){
        outcome += ipheaderBytes[i];
        if(outcome & 0x10000){
            outcome = (outcome & 0xFFFF) + 1;
        }
    } 
    // Flip bits using tilde 
    return static_cast<uint16_t>(~outcome & 0xFFFF);
}
*/

uint16_t checksumCalc(const void* data, size_t length) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    uint32_t sum = 0;

    // sum 16-bit words (big-endian)
    while (length > 1) {
        uint16_t word = (b[0] << 8) | b[1];
        sum += word;
        b += 2;
        length -= 2;
    }
    if (length == 1) { // odd byte: pad with zero
        uint16_t word = (b[0] << 8);
        sum += word;
    }

    // fold carries
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);

    uint16_t res = static_cast<uint16_t>(~sum);
    if (res == 0) res = 0xFFFF;   // UDP special-case
    return res;
}



void checkSum(char* signature_buffer, int sock, sockaddr_in server_addr, int port3){
    
    // Check if port number is valid
    cout << "port 3 number: " << port3 << endl;

    uint32_t receivedNumber;
    memcpy(&receivedNumber, signature_buffer + 1, sizeof(int));
    
    server_addr.sin_port = htons(port3);
    int sent = sendto(sock, &receivedNumber, sizeof(receivedNumber), 0,
        (sockaddr *)&server_addr, sizeof(server_addr));
    cout << "Checksum amount sent: " << sent << endl;
    
    if (sent < 0) {
        perror("sendto failed");
        close(sock);
        return;
    }

    char third_reply_buffer[470];
    char checksumBytes[2];
    char addressBytes[4];
    
    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);
    
    // Store reply in a buffer after sending signature
    int received3 = recvfrom(sock, third_reply_buffer, sizeof(third_reply_buffer), 0,
        (sockaddr *)&from_addr, &from_len);
    cout << "Amount received for checksum first receive: " << received3 << endl;
    if (received3 < 0) {
        std::cout << "received failed" << std::endl;
    } else {
        cout << "Checksum first receive: " << third_reply_buffer << endl;
        // Read last 6 bytes into a useful buffer since values change each run
        memcpy(checksumBytes, third_reply_buffer + (received3 - 6), 2);
        memcpy(addressBytes, third_reply_buffer + (received3 - 4), 4);
    }

    cout << "Checksum bytes in network order: " << endl;
    for(int i = 0; i < 2; i++){
        printf("%02X ", (unsigned char)checksumBytes[i]);
    }

    cout << "Ip Address bytes in network order: " << endl;
    for(int i = 0; i < 4; i++){
        printf("%02X ", (unsigned char)addressBytes[i]);
    }

    // Convert the address bytes to a string that is used as src addr in Ipv4 packet header
    char ipString[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, addressBytes, ipString, INET_ADDRSTRLEN);
    std::string addrString(ipString);

    cout << "The address: " << addrString << endl;

    char encapsulatedPacket[1024];
    char packetHeader[32];

    // try assigning as 32 instead of sizeof ..
    //int pkt_len = sizeof(struct ip) + sizeof(struct udphdr) + 4;
    int pkt_len = 32;

    //socklen_t local_len = sizeof(local_addr);
    sockaddr_in local_addr{};
    socklen_t local_len = sizeof(local_addr);
    
    if(connect(sock,(struct sockaddr*)&server_addr, sizeof(server_addr)) == -1){
        perror("connect failed"); 
    } else {
        if (getsockname(sock, (sockaddr*)&local_addr, &local_len) == -1) {
            perror("getsockname failed");
            
            } else {
                char local_ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &local_addr.sin_addr, local_ip_str, sizeof(local_ip_str));
                std::cout << "local IP: " << local_ip_str
                        << "  local port: " << ntohs(local_addr.sin_port) << std::endl;
        }
    }


    struct ip *ipHeader = (struct ip *) packetHeader;
    struct udphdr *udpHeader = (struct udphdr *) (packetHeader + sizeof(struct ip));
    char *data = packetHeader + sizeof(struct ip) + sizeof(struct udphdr);

    ipHeader->ip_hl = 5;
    ipHeader->ip_v = 4;
    ipHeader->ip_tos = 0;
    ipHeader->ip_len = htons(pkt_len);
    ipHeader->ip_id = htons(0);
    ipHeader->ip_ttl = 64;
    ipHeader->ip_p = 17;
    ipHeader->ip_off = 0;
    ipHeader->ip_sum = 0;
    ipHeader->ip_dst.s_addr = inet_addr("130.208.246.98");
    ipHeader->ip_src.s_addr = inet_addr(ipString);

    ipHeader->ip_sum = checksumCalc(ipHeader, 20);

    //ipHeader->ip_sum = checksumCalc((uint16_t*)ipHeader, sizeof(struct ip) / 2);

    //uint16_t local_port = ntohs(local_addr.sin_port);
    udpHeader->uh_sport = local_addr.sin_port;
    udpHeader->uh_dport = htons(port3);
    udpHeader->uh_ulen = htons(sizeof(struct udphdr) + 4);
    // Set after computing checksum
    udpHeader->uh_sum = 0;

    // TODO: Compute checksum
    // First we have to create psuedo header which we use for the checksum along with udp header and udp data
    uint8_t pseudoHeader[12];
    memcpy(pseudoHeader + 0, &ipHeader->ip_src, 4);
    memcpy(pseudoHeader + 4, &ipHeader->ip_dst, 4);
    pseudoHeader[8] = 0;
    pseudoHeader[9] = 17;
    
    uint16_t udpLen = htons(sizeof(struct udphdr) + 4);
    memcpy(pseudoHeader + 10, &udpLen, 2);
    
    // Add all headers and data for checksum calculations
    size_t cksum_len = sizeof(pseudoHeader) + sizeof(struct udphdr) + 4;
    std::vector<uint8_t> buf(cksum_len);

    memcpy(buf.data(), pseudoHeader, sizeof(pseudoHeader));
    memcpy(buf.data() + sizeof(pseudoHeader), udpHeader, sizeof(struct udphdr));
    memcpy(buf.data() + sizeof(pseudoHeader) + sizeof(struct udphdr), data, 4);
    /*
    uint32_t net_num = htonl(receivedNumber);
    memcpy(cksum_buf, &psh, sizeof(psuedoHeader));
    memcpy(cksum_buf + sizeof(psuedoHeader), udpHeader, sizeof(struct udphdr));
    // Not sure if receivedNumber is in network order or not might need to htonl that (now net_num)
    memcpy(cksum_buf + sizeof(psuedoHeader) + sizeof(struct udphdr), &net_num, 4);
    */

    uint16_t udp_sum = (checksumCalc(buf.data(), buf.size()));
    udpHeader->uh_sum = udp_sum;
    //uint16_t calc = checksumCalc((uint16_t*)udpChecksumHeader, 24 / 2);
    printf("Computed checksum: 0x%04X\n", udpHeader->uh_sum);

    printf("\n the bytes: %02X \n", (unsigned short)udpHeader->uh_sum);

    std::cout << "\nChecksum Packet header: " << std::endl;
    for (int i = 0; i < sizeof(packetHeader); i++) {
        printf("%02X ", (unsigned char)packetHeader[i]);
    }
    
    //printf("\n The checksum result is: %02X\n", (unsigned char)checksumResult);

    memcpy(encapsulatedPacket, packetHeader, pkt_len);

    int sent2 = sendto(sock, encapsulatedPacket, pkt_len, 0,
        (sockaddr *)&server_addr, sizeof(server_addr));
    cout << "Checksum amount sent second time: " << sent2 << endl;
    
    cout << "Packet lenght: " << pkt_len << endl;
    cout << "Amount sent: " << sent2 << endl;

    if (sent2 < 0) {
        perror("sendto failed");
        close(sock);
        return;
    }


}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        perror("Incorrect amount of arguments");
        exit(0);
    }

    int sock; 
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("Socket creation failed");
        exit(0);
    }
    char* ip_string = argv[1];
    int port1 = std::stoi(argv[2]);
    int port2 = std::stoi(argv[3]);
    int port3 = std::stoi(argv[4]);
    int port4 = std::stoi(argv[5]);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip_string, &server_addr.sin_addr) != 1) {
        std::cout << "IP address is weird" << std::endl;
    } 
    
    server_addr.sin_port = htons(port1);
    std::string names = "benjaminr23,sindrib23,oliver23";
    int secretNumber = 32500;

    char buffer[1024];
    buffer[0] = 'S';

    uint32_t netSecret = htonl(secretNumber);
    memcpy(buffer + 1, &netSecret, sizeof(netSecret));
    memcpy(buffer + 1 + sizeof(netSecret), names.data(), names.size());

    int totalLen = 1 + sizeof(netSecret) + names.size();
    for (int i = 0; i < totalLen; i++) {
        printf("%02X ", (unsigned char)buffer[i]);
    }
    printf("\n");
    

    int sent = sendto(sock, buffer, totalLen, 0,
        (sockaddr *)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        perror("sendto failed");
        close(sock);
        return 1;
    }

    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char rec_buffer[5];
    sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);
    int received = recvfrom(sock, rec_buffer, sizeof(rec_buffer), 0,
        (sockaddr *)&from_addr, &from_len);
    //std::cout << "Received amount for first reply: " << received << std::endl;
    if (received < 0) {
        std::cout << "received failed" << std::endl;
    }

    for (int i = 0; i < sizeof(int) + sizeof(char); i++) {
        int numb = rec_buffer[i];
        std::cout << numb << endl;
    }
   
    std::cout << '\n';
    int receivedNumber;
    int groupID = rec_buffer[0];
    
    memcpy(&receivedNumber, rec_buffer + 1, sizeof(receivedNumber));
    receivedNumber = ntohl(receivedNumber); 
    receivedNumber = receivedNumber ^ secretNumber;
    receivedNumber = htonl(receivedNumber);
    // std::cout << "Group Id: " << groupID << std::endl;
    // std::cout << "Challenger number: " << receivedNumber << std::endl;
    // std::cout << '\n';

    char signature_buffer[5];
    signature_buffer[0] = groupID;
    // std::cout << "Group ID: " << groupID << std::endl;
    // std::cout << "Signature buffer: " << std::endl;

    
    memcpy(signature_buffer + 1, &receivedNumber, sizeof(receivedNumber));
    for (size_t i = 0; i < 5; i++)
    {
        std::cout << (int)signature_buffer[i];
    }
    std::cout << std::endl;

    // for (int i = 0; i < 5; i++) {
    //     printf("Last signature buffer: %02X \n", (unsigned char)signature_buffer[i]);
    // }

    //cout << "total lenght when sending 2nd time: " << totalLen << endl;

    int sent2 = sendto(sock, signature_buffer, sizeof(signature_buffer), 0,
        (sockaddr *)&server_addr, sizeof(server_addr));
    if (sent2 < 0) {
        perror("signatureBuffer failed");
        close(sock);
        return 1;
    }

    //Second reply
    char second_reply_buffer[69];
 
    int received2 = recvfrom(sock, second_reply_buffer, sizeof(second_reply_buffer), 0,
        (sockaddr *)&from_addr, &from_len);
    //std::cout << "Received amount: " << received2 << std::endl;
    if (received2 < 0) {
        std::cout << "received failed" << std::endl;
    }

    std::cout << "Second reply buffer: " << second_reply_buffer << std::endl;
    // std::cout << std::endl;

    evilBit(signature_buffer, sock, server_addr, port2);

    checkSum(signature_buffer, sock, server_addr, port3);

    return 0;

}