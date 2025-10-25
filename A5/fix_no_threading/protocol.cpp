#include "protocol.h"
#include <arpa/inet.h>
#include <stdexcept>

std::vector<uint8_t> Protocol::encodeMessage(const std::string& command) {
    // Truncate command/payload to allowed max (assignment: 5000 bytes payload)
    std::string cmd = command;
    if (cmd.length() > PROTOCOL_MAX_PAYLOAD) {
        cmd = cmd.substr(0, PROTOCOL_MAX_PAYLOAD);
        // Note: callers may log about truncation if desired
    }

    // Calculate total length: SOH(1) + length(2) + STX(1) + command + ETX(1)
    uint16_t totalLength = static_cast<uint16_t>(1 + 2 + 1 + cmd.length() + 1);
    
    // Validate total length does not exceed max frame size
    if (totalLength > PROTOCOL_MAX_FRAME) {
        cmd = cmd.substr(0, PROTOCOL_MAX_PAYLOAD - (totalLength - PROTOCOL_MAX_FRAME));
        totalLength = static_cast<uint16_t>(1 + 2 + 1 + cmd.length() + 1);
    }

    std::vector<uint8_t> message;
    message.reserve(totalLength); // Reserving space for the entire message before adding elements 
    
    // Add SOH
    message.push_back(SOH);
    
    // Add length as big-endian (network) bytes
    message.push_back(static_cast<uint8_t>((totalLength >> 8) & 0xFF)); // high byte
    message.push_back(static_cast<uint8_t>(totalLength & 0xFF));        // low byte
    
    // Add STX
    message.push_back(STX);
    
    // Add command (truncated)
    for (char c : cmd) {
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
    
    if (data.size() < PROTOCOL_MIN_FRAME) return false;
    if (data.size() > PROTOCOL_MAX_FRAME) return false; 
    
    return declaredLength == data.size();
}

uint16_t Protocol::getMessageLength(const std::vector<uint8_t>& data) {
    if (data.size() < 3) return 0;
    uint16_t length = (static_cast<uint16_t>(data[1]) << 8) | static_cast<uint16_t>(data[2]);
    return length;
}

std::string Protocol::extractCommand(const std::vector<uint8_t>& data) {
    return decodeMessage(data);
}