#pragma once
#include "Types.hpp"
#include <cstdint>
#include <vector>
#include <string_view>
#include <cstring>

#ifdef __linux__
#include <endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htobe16(x) OSSwapHostToBigInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#endif

namespace trading {

// ITCH 5.0 messages are big-endian network layout
#pragma pack(push, 1)

struct AddOrderMessage {
    char message_type; // 'A'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;
    uint64_t order_reference_number;
    char buy_sell_indicator; // 'B' or 'S'
    uint32_t shares;
    char stock[8];
    uint32_t price;
};

#pragma pack(pop)

class ItchParser {
public:
    static inline uint64_t parse_uint64(uint64_t val) { return htobe64(val); }
    static inline uint32_t parse_uint32(uint32_t val) { return htobe32(val); }

    /**
     * @brief Zero-copy parsing over mapped network/file buffer
     */
    static void parse_message(const char* buffer, size_t length, auto& callback) {
        if (length == 0) return;

        char type = buffer[0];
        switch (type) {
            case 'A': {
                if (length < sizeof(AddOrderMessage)) return;
                const auto* msg = reinterpret_cast<const AddOrderMessage*>(buffer);
                
                OrderId id = parse_uint64(msg->order_reference_number);
                Side side = (msg->buy_sell_indicator == 'B') ? Side::BUY : Side::SELL;
                Quantity qty = parse_uint32(msg->shares);
                Price price = parse_uint32(msg->price);
                
                callback(OperationType::ADD, id, price, qty, side);
                break;
            }
            // Real implementation would handle 'E' (Execute), 'X' (Cancel), etc.
            default:
                break;
        }
    }
};

} // namespace trading
