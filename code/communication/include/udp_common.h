#pragma once
#ifndef UDP_COMMON_H
#define UDP_COMMON_H

#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdint>
#include <cerrno>
#include <chrono>


#define PC_IP "192.168.2.100"           // PC 的实际 IP，请按现场网络修改
#define PYNQ_IP "192.168.2.99"         // PYNQ 的实际 IP，请按现场网络修改
#define ASCEND_310B_IP "192.168.2."  // 310B 的实际 IP，请按现场网络修改
#define PYNQ_PORT 9090                   // 310B 监听 PYNQ 消息的端口
#define PC_PORT 5555                      // 310B 与 PC 通信的端口


inline uint64_t get_timestamp_us() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}



#pragma pack(push, 1)

struct UdpHeader {
    uint16_t frame_id;    // 帧 ID，发送时递增
    uint16_t seq_num;    // 序列号，发送时递增
    uint16_t total; // 该帧总包数
    uint16_t payload_len;// 有效载荷长度
    uint64_t timestamp=get_timestamp_us();  // 发送时的时间戳，单位微秒
};

#pragma pack(pop)


namespace udp_net{

    // ================= 基础配置 =================
    // 接收缓冲区大小：UDP 单包最大受 MTU 限制（通常 1500 字节）。
    // 此处设为 4096 仅用于测试，生产环境发送视频需按 MTU 分片或使用 SO_SNDBUF/RCVBUF 调优。
    constexpr int BUF_SIZE = 4096;

    // 视频参数：最小可行链路固定为 1280x720@30fps。
    constexpr int VIDEO_WIDTH = 1280;
    constexpr int VIDEO_HEIGHT = 720;
    constexpr int VIDEO_FPS = 30;

    // RGB24 每帧字节数 = width * height * 3。
    constexpr int RGB_FRAME_BYTES = VIDEO_WIDTH * VIDEO_HEIGHT * 3;

    // 应用层分片负载长度：1200 字节可以规避常见 MTU 导致的 IP 分片。
    constexpr int UDP_PAYLOAD_BYTES = 1200;

    // 接收端每 30 帧回一次 ACK，降低控制开销。
    constexpr int ACK_EVERY_N_FRAMES = 30;

    // 双端口隔离：310B 用不同端口区分 PC（结果回传）和 PYNQ（视频流接收）
    constexpr int PORT_PC = PC_PORT;      // 与 PC 通信端口
    constexpr int PORT_PYNQ = PYNQ_PORT;  // 与 PYNQ 通信端口

    /**
     * @brief 创建 UDP Socket 文件描述符
     * @return 成功返回 fd (>=0)，失败返回 -1
     */
    inline int create_socket() {
        // AF_INET: IPv4 | SOCK_DGRAM: 数据报协议(UDP) | 0: 默认协议
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            std::cerr << "[UDP_ERR] 创建 Socket 失败: " << strerror(errno) << std::endl;
            return -1;
        }
        return fd;
    }

    /**
     * @brief 将 Socket 绑定到本机指定端口
     * @param fd   Socket 描述符
     * @param port 监听端口号
     * @return 成功返回 true
     */
    inline bool bind_port(int fd, int port) {
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;           // IPv4
        addr.sin_addr.s_addr = INADDR_ANY;        // 绑定所有网卡 IP（0.0.0.0）
        addr.sin_port        = htons(port);       // ⚠️ htons: 主机字节序转网络字节序（大端）

        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[UDP_ERR] 绑定端口 " << port << " 失败: " << strerror(errno) << std::endl;
            return false;
        }
        std::cout << "[UDP] 成功绑定端口 " << port << std::endl;
        return true; 
    }

    /**
     * @brief 构造目标设备的 IPv4 地址结构体
     * @param ip   点分十进制字符串，如 "192.168.1.20"
     * @param port 目标端口
     * @return sockaddr_in 结构体，若 IP 非法则返回全零结构体
     */
    inline sockaddr_in make_addr(const std::string& ip, int port) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);

        // inet_pton: 安全的 IP 字符串转二进制结构体，兼容 IPv4/IPv6 扩展
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            std::cerr << "[UDP_ERR] 非法 IP 地址: " << ip << std::endl;
            return {}; // 返回零值结构体供调用方校验
        }
        return addr;
    }

    /**
     * @brief 向指定地址发送数据
     * @param fd      Socket 描述符
     * @param msg     数据内容（std::string 仅用于测试，生产环境应改用 const void* + size）
     * @param dst     目标地址结构体
     * @return 发送字节数，<0 表示失败
     */
    inline int send_msg(int fd, const std::string& msg, const sockaddr_in& dst) {
        int sent = sendto(fd, msg.c_str(), msg.size(), 0,
                          reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
        if (sent < 0) {
            std::cerr << "[UDP_ERR] 发送失败: " << strerror(errno) << std::endl;
        }
        return sent;
    }

    /**
     * @brief 接收数据（默认阻塞，直到收到数据包）
     * @param fd      Socket 描述符
     * @param src     [输出] 发送方地址（传 nullptr 则自动分配临时结构体）
     * @param srclen  [输出] 地址结构体长度
     * @return 接收到的字符串，空字符串表示接收失败或连接关闭
     * 
     *    低延迟提示：生产环境视频流应改为 recvfrom(fd, raw_buf, max_len, 0, src, srclen)
     *    直接写入预分配的 uint8_t 缓冲区，避免 std::string 内存分配开销。
     */
    inline std::string recv_msg(int fd, sockaddr_in* src = nullptr, socklen_t* srclen = nullptr) {
        char buf[BUF_SIZE];
        sockaddr_in tmp_src{};
        socklen_t tmp_len = sizeof(tmp_src);
        
        // 若调用方未提供地址存储位置，则使用局部变量接收（不影响数据读取）
        if (!src) { src = &tmp_src; srclen = &tmp_len; }

        int n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                         reinterpret_cast<sockaddr*>(src), srclen);
        
        if (n <= 0) {
            if (n < 0) std::cerr << "[UDP_ERR] 接收失败: " << strerror(errno) << std::endl;
            return "";
        }
        buf[n] = '\0'; // 添加 C 字符串结束符，便于安全打印
        return std::string(buf, n);
    }



}




#endif