#pragma once

#include <cstdint>
#include <type_traits>

constexpr std::uint32_t fieldLength = 64;
constexpr std::uint32_t maxParameters = 4;
constexpr std::uint32_t resultsLength = 256;

enum class MessageType : std::uint8_t {
    Request  = 1,
    Response = 2,
    Event    = 3
};

struct Message {
    std::uint32_t requestId;
    MessageType type;

    char methodName[fieldLength];
    char parameters[maxParameters][fieldLength];
    char results[resultsLength];
};

// Messages are sent over the socket as raw bytes (sizeof (Message) per message).
static_assert (std::is_trivially_copyable_v<Message>);
