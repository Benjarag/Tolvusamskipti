#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <vector>
#include <string>
#include <cstdint>

// Protocol constants
#define SOH 0x01
#define STX 0x02
#define ETX 0x03

class Protocol {
public:
    static std::vector<uint8_t> encodeMessage(const std::string& command);
    static std::string decodeMessage(const std::vector<uint8_t>& data);
    static bool validateMessage(const std::vector<uint8_t>& data);
    
    // Helper functions
    static uint16_t getMessageLength(const std::vector<uint8_t>& data);
    static std::string extractCommand(const std::vector<uint8_t>& data);
};

#endif