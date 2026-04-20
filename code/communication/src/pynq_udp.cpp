#include "udp_common.h"                             // 引入 UDP 公共工具头文件
#include <chrono>                                    // 引入节拍控制
#include <iostream>                                  // 引入标准输出流
#include <string>                                    // 引入字符串类型
#include <thread>                                   // 引入线程休眠

using namespace udp_net;                             // 使用 udp_net 命名空间

int main() {                                         // 主函数入口
    int fd_pynq = create_socket();                   // 创建 PYNQ 侧 socket
    if (fd_pynq < 0) {                               // 判断创建是否失败
        return 1;                                    // 返回错误码
    }                                                // 结束失败判断

    if (!bind_ip_port(fd_pynq, PYNQ_IP, PYNQ_PORT)) { // 绑定 PYNQ 本机 IP 与监听端口
        close(fd_pynq);                              // 关闭 socket
        return 1;                                    // 返回错误码
    }                                                // 结束绑定判断

    sockaddr_in to_310b = make_addr(ASCEND_310B_IP, PYNQ_PORT); // 构造 310B 目标地址
    timeval timeout{};                               // 定义 ACK 超时
    timeout.tv_sec = ACK_TIMEOUT_SEC;                // 设置秒级超时
    timeout.tv_usec = 0;                             // 设置微秒为 0
    setsockopt(fd_pynq, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); // 设置 ACK 接收超时

    int seq = 0;                                     // 定义消息序号
    std::cout << "[PYNQ] start ACK loop" << std::endl; // 打印启动信息

    while (true) {                                   // 持续发送与接收循环
        std::string msg = "HELLO_FROM_PYNQ seq=" + std::to_string(seq); // 构造发送消息
        if (send_msg(fd_pynq, msg, to_310b) < 0) {   // 发送到 310B
            std::cout << "[PYNQ] send failed" << std::endl; // 打印发送失败日志
            std::this_thread::sleep_for(std::chrono::seconds(1)); // 失败后稍等
            continue;                                // 继续下一轮
        }                                            // 结束发送判断

        std::cout << "[PYNQ] send ok: " << msg << std::endl; // 打印发送成功日志

        sockaddr_in src{};                           // 定义 ACK 源地址
        socklen_t srclen = sizeof(src);              // 定义 ACK 源长度
        std::string ack = recv_msg(fd_pynq, &src, &srclen); // 接收 ACK
        if (ack.empty()) {                           // 判断是否收到 ACK
            std::cout << "[PYNQ] recv ACK timeout" << std::endl; // 打印超时日志
        } else {                                     // 收到 ACK 分支
            std::cout << "[PYNQ] recv ACK: " << ack << std::endl; // 打印 ACK 内容
        }                                            // 结束 ACK 判断

        ++seq;                                       // 序号自增
        std::this_thread::sleep_for(std::chrono::seconds(1)); // 每秒发送一次
    }                                                // 结束循环

    close(fd_pynq);                                  // 关闭 socket
    return 0;                                        // 返回成功码
}                                                    // 主函数结束
