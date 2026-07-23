#pragma once
// VoN network packet structures (matching ArmA 3 VoN format)

#include <cstdint>
#include <cstring>


namespace Poseidon
{
enum class VoNChatChannel : uint8_t
{
    Direct = 0,
    Vehicle = 1,
    Group = 2,
    Side = 3,
    Global = 4
};

enum class VoNCommand : uint8_t
{
    Create = 1,
    Remove = 4,
    PeerInfo = 5,
    PeerCliques = 6,
    EncapsulateVoice = 7,
    ResetConnection = 8
};

// Transmitter-side pipeline health. The transmit HUD must not rely on key
// state alone: with a dead microphone or an unassigned voice identity the
// key is held, the indicator shows, and nothing is sent.
enum class VoNTransmitHealth : uint8_t
{
    Off = 0,          // transmit not requested (PTT released)
    Transmitting = 1, // frames captured, encoded and handed to the network recently
    NoCapture = 2,    // capture device or recorder missing — nothing can be sent
    NotConnected = 3, // sender identity not assigned or transport sink not wired
    Stalled = 4       // transmit requested but no packet produced within the window
};

static constexpr uint32_t VON_MAGIC_CTRL = 0xc1c1a027;
static constexpr uint32_t VON_MAGIC_DATA = 0xdada8fc1;

#pragma pack(push, 1)

struct VoNDataPacket
{
    uint32_t magic;
    uint32_t channel; // sender player ID
    VoNChatChannel chatChan;
    uint64_t origin;  // sample offset for ordering
    uint16_t samples; // decoded sample count
    uint16_t size;    // encoded byte count
    // uint8_t data[] follows

    static constexpr int HEADER_SIZE = 21; // 4+4+1+8+2+2

    void init(uint32_t sender, VoNChatChannel ch, uint64_t orig, uint16_t samp, uint16_t sz)
    {
        magic = VON_MAGIC_DATA;
        channel = sender;
        chatChan = ch;
        origin = orig;
        samples = samp;
        size = sz;
    }

    bool isValid() const { return magic == VON_MAGIC_DATA && size > 0; }

    const uint8_t* payload() const { return reinterpret_cast<const uint8_t*>(this) + HEADER_SIZE; }
    uint8_t* payload() { return reinterpret_cast<uint8_t*>(this) + HEADER_SIZE; }
    int totalSize() const { return HEADER_SIZE + size; }
};

struct VoNControlPacket
{
    uint32_t magic;
    uint32_t channel; // sender player ID
    VoNCommand command;
    uint16_t size; // payload size
    // uint8_t data[] follows

    static constexpr int HEADER_SIZE = 11;

    void init(uint32_t sender, VoNCommand cmd, uint16_t sz)
    {
        magic = VON_MAGIC_CTRL;
        channel = sender;
        command = cmd;
        size = sz;
    }

    bool isValid() const { return magic == VON_MAGIC_CTRL; }

    const uint8_t* payload() const { return reinterpret_cast<const uint8_t*>(this) + HEADER_SIZE; }
    uint8_t* payload() { return reinterpret_cast<uint8_t*>(this) + HEADER_SIZE; }
    int totalSize() const { return HEADER_SIZE + size; }
};

#pragma pack(pop)

// Helper to identify packet type from raw buffer
inline bool vonIsDataPacket(const void* buf, int len)
{
    if (len < 4)
        return false;
    uint32_t m;
    std::memcpy(&m, buf, 4);
    return m == VON_MAGIC_DATA;
}

inline bool vonIsControlPacket(const void* buf, int len)
{
    if (len < 4)
        return false;
    uint32_t m;
    std::memcpy(&m, buf, 4);
    return m == VON_MAGIC_CTRL;
}

} // namespace Poseidon
