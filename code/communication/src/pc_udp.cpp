#include "udp_common.h"                             // 引入 UDP 公共工具头文件
#include <iostream>                                  // 引入标准输出流

using namespace udp_net;                             // 使用 udp_net 命名空间

int main() {                                         // 主函数入口
    int fd_pc = create_socket();                     // 创建 PC 侧 UDP socket
    if (fd_pc < 0) {                                 // 判断创建是否失败
        return 1;                                    // 返回错误码
    }                                                // 结束失败判断

    if (!bind_ip_port(fd_pc, PC_IP, PC_PORT)) {     // 绑定 PC 本机 IP 与监听端口
        close(fd_pc);                                // 关闭 socket
        return 1;                                    // 返回错误码
    }                                                // 结束绑定判断

    std::cout << "[PC] wait ACK loop" << std::endl; // 打印启动信息

    while (true) {                                   // 持续收发循环
        sockaddr_in src{};                           // 定义源地址
        socklen_t srclen = sizeof(src);              // 定义源地址长度
        std::string msg = recv_msg(fd_pc, &src, &srclen); // 接收消息
        if (msg.empty()) {                           // 判断是否收到有效消息
            continue;                                // 无消息则继续等待
        }                                            // 结束空消息判断

        std::cout << "[PC] recv: " << msg << std::endl; // 打印收到的消息
        if (send_msg(fd_pc, "ACK_FROM_PC", src) < 0) { // 回发 ACK
            std::cout << "[PC] send ACK failed" << std::endl; // 打印失败日志
            continue;                                // 失败后继续循环
        }                                            // 结束发送判断

        std::cout << "[PC] send ACK ok" << std::endl; // 打印成功日志
    }                                                // 结束循环

    close(fd_pc);                                    // 关闭 socket
    return 0;                                        // 返回成功码
}                                                    // 主函数结束