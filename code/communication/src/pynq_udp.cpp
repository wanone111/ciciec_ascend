#include "udp_common.h"                        // 引入 UDP 公共工具头文件
#include <iostream>                            // 引入标准输出流
#include <string>                              // 引入字符串类型
#include <chrono>                              // 引入时间相关工具
#include <thread>                              // 引入线程休眠工具

using namespace udp_net;                       // 使用 udp_net 命名空间，简化函数调用

int main() {                                   // 主函数入口
    int fd_pynq = create_socket();             // 创建 PYNQ 侧 UDP socket
    if (fd_pynq < 0) {                         // 判断 socket 是否创建失败
        return 1;                              // 创建失败返回错误码
    }                                          // 结束创建失败判断

    if (!bind_port(fd_pynq, PORT_PYNQ)) {      // 绑定到 PYNQ 本地端口，用于接收 PC ACK
        close(fd_pynq);                        // 绑定失败时关闭 socket
        return 1;                              // 返回错误码
    }                                          // 结束绑定失败判断

    sockaddr_in pc_addr = make_addr(PC_IP, PORT_PC); // 构造 PC 目标地址结构体

    timeval timeout_cfg{};                     // 定义超时配置结构体
    timeout_cfg.tv_sec = 1;                    // 设置接收超时时间为 1 秒
    timeout_cfg.tv_usec = 0;                   // 设置微秒部分为 0
    setsockopt(fd_pynq, SOL_SOCKET, SO_RCVTIMEO, &timeout_cfg, sizeof(timeout_cfg)); // 设置 recvfrom 超时，避免永远阻塞

    uint32_t seq = 0;                          // 定义发送序号，便于日志观察

    std::cout << "[PYNQ] start loop, send to PC and wait ACK..." << std::endl; // 打印循环启动日志

    while (true) {                             // 进入持续循环，执行“发包并等 ACK”
        std::string payload = "HELLO_PYNQ_TO_PC seq=" + std::to_string(seq); // 组装本次发送的文本负载
        int sent = send_msg(fd_pynq, payload, pc_addr); // 向 PC 发送 UDP 消息

        if (sent < 0) {                        // 判断发送是否失败
            std::cerr << "[PYNQ] send failed" << std::endl; // 打印发送失败日志
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 发送失败后短暂等待再重试
            continue;                          // 跳转到下一轮循环
        }                                      // 结束发送失败判断

        std::cout << "[PYNQ] send ok, bytes=" << sent << ", payload=" << payload << std::endl; // 打印发送成功日志

        sockaddr_in src_pc{};                  // 定义结构体保存 ACK 来源地址
        socklen_t src_len = sizeof(src_pc);    // 初始化来源地址长度
        std::string ack = recv_msg(fd_pynq, &src_pc, &src_len); // 接收 PC 返回的 ACK

        if (ack.empty()) {                     // 判断 ACK 是否超时或接收失败
            std::cerr << "[PYNQ] recv ACK timeout or failed" << std::endl; // 打印 ACK 接收异常日志
        } else {                               // 进入 ACK 接收成功分支
            std::cout << "[PYNQ] recv ACK: " << ack << std::endl; // 打印 ACK 内容
        }                                      // 结束 ACK 分支判断

        ++seq;                                 // 发送序号自增
        std::this_thread::sleep_for(std::chrono::seconds(1)); // 每秒发送一次，便于观察链路稳定性
    }                                          // 结束持续循环

    close(fd_pynq);                            // 关闭 socket（正常流程下不会执行到此行）
    return 0;                                  // 返回成功码
}                                              // 主函数结束
