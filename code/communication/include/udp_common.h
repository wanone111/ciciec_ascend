#pragma once
#ifndef UDP_COMMON_H
#define UDP_COMMON_H

#include <cerrno>                                   // errno 相关定义
#include <cstring>                                  // strerror 和 memcpy
#include <iostream>                                 // 标准输出流
#include <string>                                   // std::string
#include <sys/socket.h>                             // socket/recvfrom/sendto
#include <netinet/in.h>                             // sockaddr_in
#include <arpa/inet.h>                              // inet_pton/htons
#include <unistd.h>                                 // close

#define PC_IP "192.168.137.1"                       // PC 的 IP（与 310B eth1 通信）
#define PYNQ_IP "192.168.2.99"                     // PYNQ 的 IP（与 310B eth0 通信）
#define ASCEND_310B_IP "192.168.2.2"               // 310B 的 eth0 IP（用于 PYNQ -> 310B）
#define ASCEND_310B_PC_IP "192.168.137.100"        // 310B 的 eth1 IP（用于 310B <-> PC）
#define PC_PORT 5555                                 // PC 监听端口
#define PYNQ_PORT 9090                               // PYNQ/310B 监听端口

namespace udp_net {                                  // UDP 辅助命名空间

constexpr int BUF_SIZE = 1024;                       // ACK 消息缓冲区大小
constexpr int ACK_TIMEOUT_SEC = 1;                   // ACK 接收超时时间

inline int create_socket() {                         // 创建 UDP socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);         // 申请 IPv4 UDP socket
    if (fd < 0) {                                    // 判断是否创建失败
        std::cerr << "[UDP] socket failed: " << strerror(errno) << std::endl; // 打印错误
        return -1;                                   // 返回失败码
    }                                                // 结束失败判断
    return fd;                                       // 返回 socket 描述符
}                                                    // 创建 socket 结束

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