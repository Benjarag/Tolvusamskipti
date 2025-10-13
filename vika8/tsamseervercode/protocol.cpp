#include "protocol.h"
#include <arpa/inet.h>
#include <stdexcept>

std::vector<uint8_t> Protocol::encodeMessage(const std::string& command) {
    // Calculate total length: SOH(1) + length(2) + STX(1) + command + ETX(1)
    // Length is always 2 bytes because the size will never exceed 65535 max size of command is 5000 bytes, so total length max is 5005
    uint16_t totalLength = 1 + 2 + 1 + command.length() + 1;
    
    std::vector<uint8_t> message;
    message.reserve(totalLength); // Reserving space for the entire message before adding elements 
    
    // Add SOH
    message.push_back(SOH);
    
    // Add length in network byte order
    uint16_t netLength = htons(totalLength);
    message.push_back((netLength >> 8) & 0xFF);
    message.push_back(netLength & 0xFF);
    
    // Add STX
    message.push_back(STX);
    
    // Add command
    for (char c : command) {
        message.push_back(static_cast<uint8_t>(c));
    }
    
    // Add ETX
    message.push_back(ETX);

    // Return the constructed message
    return message;
}

std::string Protocol::decodeMessage(const std::vector<uint8_t>& data) {
    if (!validateMessage(data)) {
        throw std::runtime_error("Invalid message format");
    }
    
    // Extract command between STX and ETX
    size_t stx_pos = 4; // After SOH(1) + length(2) + STX(1)
    size_t etx_pos = data.size() - 1;
    
    std::string command;
    for (size_t i = stx_pos; i < etx_pos; ++i) {
        command += static_cast<char>(data[i]);
    }
    
    return command;
}

bool Protocol::validateMessage(const std::vector<uint8_t>& data) {
    if (data.size() < 5) return false; // Minimum: SOH + length + STX + ETX
    // Check stickers
    if (data[0] != SOH) return false;
    if (data[3] != STX) return false;
    if (data[data.size()-1] != ETX) return false;
    
    // Verify length sticker matches real length
    uint16_t declaredLength = (data[1] << 8) | data[2];
    declaredLength = ntohs(declaredLength);
    
    return declaredLength == data.size();
}

uint16_t Protocol::getMessageLength(const std::vector<uint8_t>& data) {
    if (data.size() < 3) return 0;
    uint16_t length = (data[1] << 8) | data[2];
    return ntohs(length);
}

std::string Protocol::extractCommand(const std::vector<uint8_t>& data) {
    return decodeMessage(data);
}