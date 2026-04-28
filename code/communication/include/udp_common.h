#pragma once
#ifndef UDP_COMMON_H
#define UDP_COMMON_H

#include <cerrno>                                   // errno 相关定义
#include <cstdint>                                  // 定长整数类型
#include <cstring>                                  // strerror 和 memcpy
#include <iostream>                                 // 标准输出流
#include <string>                                   // std::string
#include <vector>                                   // 二进制缓冲区
#include <sys/socket.h>                             // socket/recvfrom/sendto
#include <netinet/in.h>                             // sockaddr_in
#include <arpa/inet.h>                              // inet_pton/htons
#include <unistd.h>                                 // close


// ============ IP 地址和端口定义 ============
#define PC_IP "192.168.137.1"                       // PC 的 IP（与 310B eth1 通信）
#define PYNQ_IP "192.168.2.99"                     // PYNQ 的 IP（与 310B eth0 通信）
#define ASCEND_310B_IP "192.168.2.2"               // 310B 的 eth0 IP（用于 PYNQ -> 310B）
#define ASCEND_310B_PC_IP "192.168.137.100"        // 310B 的 eth1 IP（用于 310B <-> PC）

// 视频流端口
#define PC_PORT 5555                                 // PC 监听端口（视频流接收）
#define PYNQ_PORT 9090                               // PYNQ/310B 监听端口（视频流接收）

// 控制端口
#define PC_CTRL_PORT 5566                            // PC 控制端口（推理结果发送）
#define PC_VIDEO_CTRL_PORT 5567                      // PC 视频控制端口（播放指令发送）

// 视频参数
#define VIDEO_WIDTH 640                              // 默认视频宽度（可按现场修改）
#define VIDEO_HEIGHT 480                             // 默认视频高度（可按现场修改）
#define VIDEO_FPS 15                                 // 默认视频帧率（可按现场修改）


namespace udp_net {                                  // UDP 辅助命名空间

constexpr int BUF_SIZE = 1024;                       // ACK 消息缓冲区大小
constexpr int ACK_TIMEOUT_SEC = 1;                   // ACK 接收超时时间
constexpr std::size_t UDP_SAFE_PAYLOAD = 1200;       // 单包安全载荷，避免链路层分片
constexpr std::size_t MAX_VIDEO_FRAME_BYTES = 16 * 1024 * 1024; // 单帧最大字节数，避免异常包占满内存
constexpr uint16_t MAX_VIDEO_FRAG_CNT = static_cast<uint16_t>(
    (MAX_VIDEO_FRAME_BYTES + UDP_SAFE_PAYLOAD - 1) / UDP_SAFE_PAYLOAD); // 单帧最大分片数
constexpr uint32_t VIDEO_MAGIC = 0x56494430;         // 协议魔数 "VID0"
constexpr uint8_t VIDEO_VERSION = 1;                 // 协议版本号
constexpr uint8_t PIX_FMT_RGB24 = 1;                 // 像素格式枚举：RGB24
constexpr uint8_t PIX_FMT_MJPEG = 2;                 // 像素格式枚举：MJPEG
constexpr uint8_t PIX_FMT_NV12 = 3;                  // 像素格式枚举：NV12 (YUV420SP)
constexpr int FRAME_TIMEOUT_MS = 800;                // 帧重组超时时间，超时丢帧
constexpr int DEFAULT_UDP_RCVBUF_BYTES = 8 * 1024 * 1024; // 默认 UDP 接收缓冲区大小
constexpr int DEFAULT_UDP_SNDBUF_BYTES = 8 * 1024 * 1024; // 默认 UDP 发送缓冲区大小

enum class MsgType : uint8_t {                       // 协议消息类型
    VIDEO_FRAGMENT = 1,                              // 视频分片消息
    CONTROL_TEXT = 2,                                // 文本控制消息
    VIDEO_CTRL_CMD = 3                               // 视频播放控制命令
};

#pragma pack(push, 1)                                // 强制 1 字节对齐，避免不同平台填充差异
struct VideoPacketHeader {                           // 视频 UDP 分片包头
    uint32_t magic;                                  // 协议魔数，固定为 VIDEO_MAGIC
    uint8_t version;                                 // 协议版本
    uint8_t msg_type;                                // 消息类型，见 MsgType
    uint16_t header_bytes;                           // 包头字节数，便于后续扩展
    uint32_t frame_id;                               // 帧序号，发送端递增
    uint16_t frag_idx;                               // 当前分片序号，从 0 开始
    uint16_t frag_cnt;                               // 当前帧总分片数
    uint16_t payload_bytes;                          // 当前分片载荷字节数
    uint16_t width;                                  // 视频宽度
    uint16_t height;                                 // 视频高度
    uint8_t pixel_format;                            // 像素格式，当前支持 RGB24/MJPEG/NV12
    uint8_t reserved0;                               // 预留字段，后续扩展
    uint64_t timestamp_us;                           // 采集时间戳（微秒）
};
#pragma pack(pop)

static_assert(sizeof(VideoPacketHeader) == 32, "VideoPacketHeader size must be 32 bytes"); // 编译期校验

inline VideoPacketHeader host_to_net(VideoPacketHeader h) { // 将主机字节序包头转换为网络字节序
    h.magic = htonl(h.magic);                        // 转换 32 位魔数
    h.header_bytes = htons(h.header_bytes);          // 转换 16 位包头长度
    h.frame_id = htonl(h.frame_id);                  // 转换 32 位帧号
    h.frag_idx = htons(h.frag_idx);                  // 转换 16 位分片序号
    h.frag_cnt = htons(h.frag_cnt);                  // 转换 16 位分片总数
    h.payload_bytes = htons(h.payload_bytes);        // 转换 16 位载荷长度
    h.width = htons(h.width);                        // 转换 16 位宽度
    h.height = htons(h.height);                      // 转换 16 位高度
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    h.timestamp_us = __builtin_bswap64(h.timestamp_us); // 转换 64 位时间戳
#endif
    return h;                                        // 返回网络字节序结果
}

inline VideoPacketHeader net_to_host(VideoPacketHeader h) { // 将网络字节序包头转换为主机字节序
    h.magic = ntohl(h.magic);                        // 转换 32 位魔数
    h.header_bytes = ntohs(h.header_bytes);          // 转换 16 位包头长度
    h.frame_id = ntohl(h.frame_id);                  // 转换 32 位帧号
    h.frag_idx = ntohs(h.frag_idx);                  // 转换 16 位分片序号
    h.frag_cnt = ntohs(h.frag_cnt);                  // 转换 16 位分片总数
    h.payload_bytes = ntohs(h.payload_bytes);        // 转换 16 位载荷长度
    h.width = ntohs(h.width);                        // 转换 16 位宽度
    h.height = ntohs(h.height);                      // 转换 16 位高度
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    h.timestamp_us = __builtin_bswap64(h.timestamp_us); // 转换 64 位时间戳
#endif
    return h;                                        // 返回主机字节序结果
}

inline bool parse_video_header(const std::vector<uint8_t>& packet, VideoPacketHeader* out) { // 解析并校验视频包头
    if (!out) {                                      // 判空保护
        return false;                                // 输出参数为空直接失败
    }
    if (packet.size() < sizeof(VideoPacketHeader)) { // 必须至少包含完整包头
        return false;                                // 长度不足
    }

    VideoPacketHeader net_hdr{};                     // 临时网络字节序包头
    std::memcpy(&net_hdr, packet.data(), sizeof(net_hdr)); // 拷贝包头原始字节
    VideoPacketHeader hdr = net_to_host(net_hdr);    // 转为主机字节序

    if (hdr.magic != VIDEO_MAGIC) {                  // 校验魔数
        return false;                                // 非视频协议包
    }
    if (hdr.version != VIDEO_VERSION) {              // 校验版本
        return false;                                // 版本不匹配
    }
    if (hdr.header_bytes != sizeof(VideoPacketHeader)) { // 校验包头长度
        return false;                                // 长度不匹配
    }
    if (hdr.frag_cnt == 0 || hdr.frag_cnt > MAX_VIDEO_FRAG_CNT) { // 校验分片数量范围
        return false;                                // 分片数量非法
    }
    if (hdr.frag_idx >= hdr.frag_cnt) {              // 校验分片序号范围
        return false;                                // 分片序号非法
    }
    if (hdr.payload_bytes == 0 || hdr.payload_bytes > UDP_SAFE_PAYLOAD) { // 校验载荷长度范围
        return false;                                // 载荷长度非法
    }
    if (hdr.msg_type != static_cast<uint8_t>(MsgType::VIDEO_FRAGMENT)) { // 校验消息类型
        return false;                                // 不是视频分片
    }
    if (packet.size() != sizeof(VideoPacketHeader) + hdr.payload_bytes) { // 校验包总长度
        return false;                                // 长度不一致
    }
    if (hdr.pixel_format != PIX_FMT_RGB24 && hdr.pixel_format != PIX_FMT_MJPEG && hdr.pixel_format != PIX_FMT_NV12) { // 校验像素格式
        return false;                                // 当前支持 RGB24/MJPEG/NV12
    }

    *out = hdr;                                      // 输出解析结果
    return true;                                     // 解析成功
}

inline int create_socket() {                         // 创建 UDP socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);         // 申请 IPv4 UDP socket
    if (fd < 0) {                                    // 判断是否创建失败
        std::cerr << "[UDP] socket failed: " << strerror(errno) << std::endl; // 打印错误
        return -1;                                   // 返回失败码
    }                                                // 结束失败判断
    return fd;                                       // 返回 socket 描述符
}                                                    // 创建 socket 结束

inline void tune_socket_buffers(int fd, int rcvbuf_bytes, int sndbuf_bytes) { // 设置 UDP 套接字收发缓冲区大小
    if (rcvbuf_bytes > 0) {                          // 设置接收缓冲区分支
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_bytes, sizeof(rcvbuf_bytes)) < 0) { // 设置接收缓冲区
            std::cerr << "[UDP] setsockopt SO_RCVBUF failed: " << strerror(errno) << std::endl; // 打印错误
        }
    }
    if (sndbuf_bytes > 0) {                          // 设置发送缓冲区分支
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_bytes, sizeof(sndbuf_bytes)) < 0) { // 设置发送缓冲区
            std::cerr << "[UDP] setsockopt SO_SNDBUF failed: " << strerror(errno) << std::endl; // 打印错误
        }
    }
}

inline bool bind_port(int fd, int port) {            // 将 socket 绑定到本地端口
    sockaddr_in addr{};                              // 定义本地地址结构
    addr.sin_family = AF_INET;                       // 设置 IPv4
    addr.sin_addr.s_addr = INADDR_ANY;               // 绑定所有本地地址
    addr.sin_port = htons(port);                     // 设置端口号
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { // 执行绑定
        std::cerr << "[UDP] bind " << port << " failed: " << strerror(errno) << std::endl; // 打印错误
        return false;                                // 返回失败
    }                                                // 结束绑定判断
    return true;                                     // 返回成功
}                                                    // 绑定结束

inline bool bind_ip_port(int fd, const std::string& ip, int port) { // 将 socket 绑定到指定本机 IP 和端口
    sockaddr_in addr{};                              // 定义本地地址结构
    addr.sin_family = AF_INET;                       // 设置 IPv4
    addr.sin_port = htons(port);                     // 设置端口号
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) { // 转换本机 IP 字符串
        std::cerr << "[UDP] invalid bind ip: " << ip << std::endl; // 打印错误
        return false;                                // 返回失败
    }                                                // 结束 IP 转换判断
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { // 执行绑定
        std::cerr << "[UDP] bind " << ip << ":" << port << " failed: " << strerror(errno) << std::endl; // 打印错误
        return false;                                // 返回失败
    }                                                // 结束绑定判断
    return true;                                     // 返回成功
}                                                    // 指定 IP 绑定结束

inline sockaddr_in make_addr(const std::string& ip, int port) { // 构造目标地址
    sockaddr_in addr{};                              // 定义地址结构
    addr.sin_family = AF_INET;                       // 设置 IPv4
    addr.sin_port = htons(port);                     // 设置端口号
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) { // 转换 IP 字符串
        std::cerr << "[UDP] invalid ip: " << ip << std::endl; // 打印错误
        return {};                                   // 返回空地址
    }                                                // 结束 IP 转换判断
    return addr;                                     // 返回目标地址
}                                                    // 构造地址结束

inline int send_msg(int fd, const std::string& msg, const sockaddr_in& dst) { // 发送文本消息
    int sent = sendto(fd, msg.c_str(), msg.size(), 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst)); // 调用 sendto
    if (sent < 0) {                                  // 判断发送是否失败
        std::cerr << "[UDP] send failed: " << strerror(errno) << std::endl; // 打印错误
    }                                                // 结束失败判断
    return sent;                                     // 返回发送结果
}                                                    // 发送消息结束


inline int send_bytes(int fd, const uint8_t* data, std::size_t size, const sockaddr_in& dst) { // 发送二进制消息
    int sent = sendto(fd, data, size, 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst)); // 调用 sendto
    if (sent < 0) {                                  // 判断发送是否失败
        std::cerr << "[UDP] send bytes failed: " << strerror(errno) << std::endl; // 打印错误
    }
    return sent;                                     // 返回发送结果
}

inline int recv_bytes(int fd, std::vector<uint8_t>* out, sockaddr_in* src = nullptr, socklen_t* srclen = nullptr) { // 接收二进制消息
    if (!out) {                                      // 判空保护
        return -1;                                   // 输出缓冲区为空
    }

    std::vector<uint8_t> buf(2048, 0);              // 预留接收缓冲区（大于 1200+头）
    sockaddr_in tmp_src{};                           // 临时源地址
    socklen_t tmp_len = sizeof(tmp_src);             // 临时源地址长度
    if (!src) {                                      // 未提供源地址时使用临时变量
        src = &tmp_src;                              // 指向临时源地址
        srclen = &tmp_len;                           // 指向临时长度
    }

    int n = recvfrom(fd, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(src), srclen); // 接收数据
    if (n <= 0) {                                    // 判断是否接收失败或超时
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { // 仅打印真实错误
            std::cerr << "[UDP] recv bytes failed: " << strerror(errno) << std::endl; // 打印错误
        }
        return n;                                    // 返回原始结果
    }

    out->assign(buf.begin(), buf.begin() + n);       // 拷贝有效字节到输出向量
    return n;                                        // 返回接收长度
}

inline int send_video_ctrl_cmd(int fd, const std::string& json_cmd, const sockaddr_in& dst) { // 发送视频控制命令（JSON 格式）
    int sent = sendto(fd, json_cmd.data(), json_cmd.size(), 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst)); // 调用 sendto
    if (sent < 0) {                                  // 判断发送是否失败
        std::cerr << "[UDP] send video ctrl cmd failed: " << strerror(errno) << std::endl; // 打印错误
    }
    return sent;                                     // 返回发送结果
}

inline std::string recv_msg(int fd, sockaddr_in* src = nullptr, socklen_t* srclen = nullptr) { // 接收文本消息
    char buf[BUF_SIZE]{};                            // 定义接收缓冲区
    sockaddr_in tmp_src{};                           // 定义临时源地址
    socklen_t tmp_len = sizeof(tmp_src);             // 定义临时源地址长度
    if (!src) {                                      // 判断是否提供源地址输出
        src = &tmp_src;                              // 使用临时源地址
        srclen = &tmp_len;                           // 使用临时长度
    }                                                // 结束输出判断
    int n = recvfrom(fd, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(src), srclen); // 接收数据
    if (n <= 0) {                                    // 判断是否接收失败或超时
        if (n < 0) {                                 // 判断是否为错误
            std::cerr << "[UDP] recv failed: " << strerror(errno) << std::endl; // 打印错误
        }                                            // 结束错误判断
        return {};                                   // 返回空字符串
    }                                                // 结束接收判断
    buf[n] = '\0';                                   // 补上字符串结束符
    return std::string(buf, n);                      // 返回接收到的字符串
}                                                    // 接收消息结束

}                                                    // 结束命名空间

#endif                                               // 结束头文件保护