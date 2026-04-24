#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace pose310b {

constexpr uint32_t POSE_MAGIC = 0x524f4931u; // "ROI1"
constexpr uint8_t POSE_VERSION = 1;
constexpr uint8_t POSE_MSG_ROI_FRAME = 1;
constexpr uint8_t POSE_PIXEL_FORMAT_NV12 = 1;
constexpr std::size_t POSE_HEADER_BYTES = 128;
constexpr std::size_t POSE_SAFE_UDP_PAYLOAD = 1200;

#pragma pack(push, 1)
struct PoseRoiPacketHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t msg_type;
    uint16_t header_bytes;
    uint32_t frame_id;
    uint32_t roi_id;
    uint16_t roi_index;
    uint16_t roi_count;
    uint16_t frag_idx;
    uint16_t frag_cnt;
    uint16_t payload_bytes;
    uint8_t pixel_format;
    uint8_t reserved0;
    uint16_t width;
    uint16_t height;
    uint16_t src_width;
    uint16_t src_height;
    uint16_t roi_x;
    uint16_t roi_y;
    uint16_t roi_w;
    uint16_t roi_h;
    uint16_t weight;
    uint16_t reserved1;
    uint64_t timestamp_ms;
    uint8_t reserved[72];
};
#pragma pack(pop)

static_assert(sizeof(PoseRoiPacketHeader) == POSE_HEADER_BYTES, "PoseRoiPacketHeader must be 128 bytes");

inline PoseRoiPacketHeader host_to_net(PoseRoiPacketHeader h) {
    h.magic = htonl(h.magic);
    h.header_bytes = htons(h.header_bytes);
    h.frame_id = htonl(h.frame_id);
    h.roi_id = htonl(h.roi_id);
    h.roi_index = htons(h.roi_index);
    h.roi_count = htons(h.roi_count);
    h.frag_idx = htons(h.frag_idx);
    h.frag_cnt = htons(h.frag_cnt);
    h.payload_bytes = htons(h.payload_bytes);
    h.width = htons(h.width);
    h.height = htons(h.height);
    h.src_width = htons(h.src_width);
    h.src_height = htons(h.src_height);
    h.roi_x = htons(h.roi_x);
    h.roi_y = htons(h.roi_y);
    h.roi_w = htons(h.roi_w);
    h.roi_h = htons(h.roi_h);
    h.weight = htons(h.weight);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    h.timestamp_ms = __builtin_bswap64(h.timestamp_ms);
#endif
    return h;
}

inline PoseRoiPacketHeader net_to_host(PoseRoiPacketHeader h) {
    h.magic = ntohl(h.magic);
    h.header_bytes = ntohs(h.header_bytes);
    h.frame_id = ntohl(h.frame_id);
    h.roi_id = ntohl(h.roi_id);
    h.roi_index = ntohs(h.roi_index);
    h.roi_count = ntohs(h.roi_count);
    h.frag_idx = ntohs(h.frag_idx);
    h.frag_cnt = ntohs(h.frag_cnt);
    h.payload_bytes = ntohs(h.payload_bytes);
    h.width = ntohs(h.width);
    h.height = ntohs(h.height);
    h.src_width = ntohs(h.src_width);
    h.src_height = ntohs(h.src_height);
    h.roi_x = ntohs(h.roi_x);
    h.roi_y = ntohs(h.roi_y);
    h.roi_w = ntohs(h.roi_w);
    h.roi_h = ntohs(h.roi_h);
    h.weight = ntohs(h.weight);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    h.timestamp_ms = __builtin_bswap64(h.timestamp_ms);
#endif
    return h;
}

inline bool parse_packet_header(const uint8_t* data, std::size_t size, PoseRoiPacketHeader* out) {
    if (!out || !data || size < sizeof(PoseRoiPacketHeader)) {
        return false;
    }
    PoseRoiPacketHeader net_hdr{};
    std::memcpy(&net_hdr, data, sizeof(net_hdr));
    PoseRoiPacketHeader hdr = net_to_host(net_hdr);
    if (hdr.magic != POSE_MAGIC || hdr.version != POSE_VERSION) {
        return false;
    }
    if (hdr.header_bytes != POSE_HEADER_BYTES) {
        return false;
    }
    if (hdr.msg_type != POSE_MSG_ROI_FRAME) {
        return false;
    }
    if (hdr.pixel_format != POSE_PIXEL_FORMAT_NV12) {
        return false;
    }
    if (size != sizeof(PoseRoiPacketHeader) + hdr.payload_bytes) {
        return false;
    }
    *out = hdr;
    return true;
}

struct FrameKey {
    uint32_t frame_id = 0;
    uint32_t roi_id = 0;

    bool operator==(const FrameKey& other) const {
        return frame_id == other.frame_id && roi_id == other.roi_id;
    }
};

struct FrameKeyHash {
    std::size_t operator()(const FrameKey& key) const noexcept {
        return (static_cast<std::size_t>(key.frame_id) << 32) ^ static_cast<std::size_t>(key.roi_id);
    }
};

struct PoseFrame {
    PoseRoiPacketHeader hdr{};
    std::vector<uint8_t> payload;
};

struct PoseResult {
    uint32_t frame_id = 0;
    uint64_t timestamp_ms = 0;
    float kps_conf_mean = 0.0f;
    int action_flag = 0;
    float action_conf = 0.0f;
    uint32_t roi_id = 0;
    uint16_t roi_index = 0;
};

} // namespace pose310b
