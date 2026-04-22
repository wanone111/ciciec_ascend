#include "udp_common.h"                             // 引入 UDP 公共工具头文件
#include <iostream>                                  // 引入标准输出流
#include <vector>                                    // 引入二进制缓冲区

using namespace udp_net;                             // 使用 udp_net 命名空间

int main() {                                         // 主函数入口
    int fd_pynq = create_socket();                   // 创建 PYNQ 侧 socket
    if (fd_pynq < 0) {                               // 判断创建是否失败
        return 1;                                    // 返回错误码
    }                                                // 结束失败判断

    if (!bind_ip_port(fd_pynq, ASCEND_310B_IP, PYNQ_PORT)) { // 绑定 310B eth0 与 PYNQ 监听端口
        close(fd_pynq);                              // 关闭 socket
        return 1;                                    // 返回错误码
    }                                                // 结束绑定判断
    tune_socket_buffers(fd_pynq, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES); // 增大 PYNQ 口 UDP 缓冲区

    int fd_pc = create_socket();                     // 创建 PC 侧 socket
    if (fd_pc < 0) {                                 // 判断创建是否失败
        close(fd_pynq);                              // 关闭 PYNQ socket
        return 1;                                    // 返回错误码
    }                                                // 结束失败判断

    if (!bind_ip_port(fd_pc, ASCEND_310B_PC_IP, PC_PORT)) { // 绑定 310B eth1 与 PC 监听端口
        close(fd_pynq);                              // 关闭 PYNQ socket
        close(fd_pc);                                // 关闭 PC socket
        return 1;                                    // 返回错误码
    }                                                // 结束绑定判断
    tune_socket_buffers(fd_pc, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES); // 增大 PC 口 UDP 缓冲区

    sockaddr_in pc_addr = make_addr(PC_IP, PC_PORT); // 构造 PC 目标地址

    timeval control_timeout{};                       // 定义控制通道超时
    control_timeout.tv_sec = 0;                      // 秒级超时设置为 0
    control_timeout.tv_usec = 5000;                  // 微秒级超时 5ms，避免阻塞视频转发
    setsockopt(fd_pc, SOL_SOCKET, SO_RCVTIMEO, &control_timeout, sizeof(control_timeout)); // 设置 PC 控制接收超时

    std::cout << "[310B] start relay loop (video forward + control tunnel)" << std::endl; // 打印启动信息

    sockaddr_in last_pynq_src{};                     // 保存最近一次 PYNQ 源地址
    bool has_pynq_src = false;                       // 标记是否已有可回传的 PYNQ 地址
    uint64_t pkt_cnt = 0;                            // 统计累计转发包数量

    while (true) {                                   // 持续中继循环
        std::vector<uint8_t> packet;                 // 定义视频/控制通用接收缓冲区
        sockaddr_in pynq_src{};                      // 定义 PYNQ 源地址
        socklen_t pynq_srclen = sizeof(pynq_src);    // 定义 PYNQ 源地址长度
        int n = recv_bytes(fd_pynq, &packet, &pynq_src, &pynq_srclen); // 从 PYNQ 接收数据包
        if (n > 0) {                                 // 收到有效数据包分支
            has_pynq_src = true;                     // 标记已学习到 PYNQ 地址
            last_pynq_src = pynq_src;                // 更新最新 PYNQ 地址

            if (send_bytes(fd_pc, packet.data(), packet.size(), pc_addr) < 0) { // 原样转发到 PC
                std::cout << "[310B] forward to PC failed" << std::endl; // 打印失败日志
            } else {                                 // 转发成功分支
                ++pkt_cnt;                           // 统计转发包计数
                VideoPacketHeader hdr{};             // 定义包头解析结果
                if (parse_video_header(packet, &hdr) && (pkt_cnt % 200 == 0)) { // 每 200 包打印一次关键视频日志
                    std::cout << "[310B] forward video frame=" << hdr.frame_id
                              << " frag=" << hdr.frag_idx << "/" << hdr.frag_cnt
                              << " payload=" << hdr.payload_bytes << std::endl; // 打印视频转发状态
                }
            }
        }

        std::vector<uint8_t> ctrl_packet;            // 定义 PC 控制消息接收缓冲区
        sockaddr_in pc_src{};                        // 定义 PC 源地址
        socklen_t pc_srclen = sizeof(pc_src);        // 定义 PC 源地址长度
        int ctrl_n = recv_bytes(fd_pc, &ctrl_packet, &pc_src, &pc_srclen); // 轮询接收 PC 控制消息
        if (ctrl_n > 0 && has_pynq_src) {            // 收到控制消息且已知 PYNQ 地址
            if (send_bytes(fd_pynq, ctrl_packet.data(), ctrl_packet.size(), last_pynq_src) < 0) { // 透传控制到 PYNQ
                std::cout << "[310B] forward control to PYNQ failed" << std::endl; // 打印失败日志
            } else {                                 // 透传成功分支
                std::cout << "[310B] forward control to PYNQ size=" << ctrl_packet.size() << std::endl; // 打印控制透传日志
            }
        }
    }                                                // 结束循环

    close(fd_pynq);                                  // 关闭 PYNQ socket
    close(fd_pc);                                    // 关闭 PC socket
    return 0;                                        // 返回成功码
}                                                    // 主函数结束