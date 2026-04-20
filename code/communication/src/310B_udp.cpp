#include "udp_common.h"                             // 引入 UDP 公共工具头文件
#include <iostream>                                  // 引入标准输出流

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

    sockaddr_in pc_addr = make_addr(PC_IP, PC_PORT); // 构造 PC 目标地址
    timeval timeout{};                               // 定义 ACK 超时
    timeout.tv_sec = ACK_TIMEOUT_SEC;                // 设置秒级超时
    timeout.tv_usec = 0;                             // 设置微秒为 0
    setsockopt(fd_pc, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); // 设置 PC 侧 ACK 等待超时

    std::cout << "[310B] start relay ACK loop" << std::endl; // 打印启动信息

    while (true) {                                   // 持续中继循环
        sockaddr_in pynq_src{};                      // 定义 PYNQ 源地址
        socklen_t pynq_srclen = sizeof(pynq_src);    // 定义 PYNQ 源地址长度
        std::string msg = recv_msg(fd_pynq, &pynq_src, &pynq_srclen); // 接收 PYNQ 消息
        if (msg.empty()) {                           // 判断是否收到消息
            continue;                                // 没收到就继续等
        }                                            // 结束空消息判断

        std::cout << "[310B] recv from PYNQ: " << msg << std::endl; // 打印 PYNQ 消息

        if (send_msg(fd_pc, "HELLO_FROM_310B", pc_addr) < 0) { // 向 PC 发送固定消息
            std::cout << "[310B] send to PC failed" << std::endl; // 打印失败日志
        } else {                                     // 发送成功分支
            std::cout << "[310B] send to PC ok" << std::endl; // 打印成功日志
        }                                            // 结束发送判断

        sockaddr_in pc_src{};                        // 定义 PC 源地址
        socklen_t pc_srclen = sizeof(pc_src);        // 定义 PC 源地址长度
        std::string pc_ack = recv_msg(fd_pc, &pc_src, &pc_srclen); // 接收 PC ACK
        if (pc_ack.empty()) {                        // 判断是否收到 PC ACK
            std::cout << "[310B] recv ACK from PC timeout" << std::endl; // 打印超时日志
        } else {                                     // 收到 ACK 分支
            std::cout << "[310B] recv ACK from PC: " << pc_ack << std::endl; // 打印 PC ACK
        }                                            // 结束 PC ACK 判断

        if (send_msg(fd_pynq, "ACK_FROM_310B", pynq_src) < 0) { // 向 PYNQ 回 ACK
            std::cout << "[310B] send ACK to PYNQ failed" << std::endl; // 打印失败日志
        } else {                                     // 发送成功分支
            std::cout << "[310B] send ACK to PYNQ ok" << std::endl; // 打印成功日志
        }                                            // 结束 PYNQ ACK 判断
    }                                                // 结束循环

    close(fd_pynq);                                  // 关闭 PYNQ socket
    close(fd_pc);                                    // 关闭 PC socket
    return 0;                                        // 返回成功码
}                                                    // 主函数结束