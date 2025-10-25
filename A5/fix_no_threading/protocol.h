#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <vector>
#include <string>
#include <cstdint>

// Protocol constants
#define SOH 0x01
#define STX 0x02
#define ETX 0x03
#define EOT 0x04  // End of Transmission - separates message content from hop list
// Limits
// The assignment defines a maximum payload of 5000 bytes. The protocol frame
// adds 5 bytes of framing: SOH(1) + length(2) + STX(1) + ETX(1). Therefore
// the maximum complete frame size MUST be payload + 5 = 5005 bytes. Use these
// constants consistently across the codebase so encoding/decoding and buffer
// sizes match the spec exactly.
constexpr std::size_t PROTOCOL_MIN_FRAME   = 5;       // min bytes of a valid frame (SOH + len(2) + STX + ETX)
constexpr std::size_t PROTOCOL_MAX_PAYLOAD = 5000;    // max bytes of the command/payload (per assignment)
constexpr std::size_t PROTOCOL_MAX_FRAME   = PROTOCOL_MAX_PAYLOAD + PROTOCOL_MIN_FRAME; // 5005

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