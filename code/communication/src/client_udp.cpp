#include "udp_common.h"                    // 引入 UDP 公共工具头文件
#include <iostream>                        // 引入标准输出流
#include <string>                          // 引入字符串类型
#include <cstdlib>                         // 引入通用工具函数

using namespace udp_net;                   // 使用 udp_net 命名空间，简化函数调用

int main() {                               // 主函数入口
    int fd_pc = create_socket();           // 创建 PC 侧 UDP socket
    if (fd_pc < 0) {                       // 判断创建是否失败
        return 1;                          // 创建失败返回错误码
    }                                      // 结束判断

    if (!bind_port(fd_pc, PORT_PC)) {      // 绑定到 PC 通信端口，等待 PYNQ 发包
        close(fd_pc);                      // 绑定失败关闭 socket
        return 1;                          // 返回错误码
    }                                      // 结束绑定判断

    std::cout << "[PC] start loop, wait message from PYNQ..." << std::endl; // 打印循环启动提示

    while (true) {                         // 进入持续循环，执行“收包并回 ACK”
        sockaddr_in src_pynq{};            // 定义结构体保存 PYNQ 源地址
        socklen_t src_len = sizeof(src_pynq); // 初始化源地址长度
        std::string msg = recv_msg(fd_pc, &src_pynq, &src_len); // 阻塞接收 PYNQ 消息

        if (msg.empty()) {                 // 判断是否接收失败或空消息
            std::cerr << "[PC] recv failed or empty payload" << std::endl; // 打印接收异常日志
            continue;                      // 继续下一轮循环，不退出程序
        }                                  // 结束接收失败判断

        std::cout << "[PC] recv: " << msg << std::endl; // 打印接收到的消息内容

        std::string ack = "ACK_FROM_PC";  // 构造固定 ACK 文本
        int sent = sendto(fd_pc, ack.c_str(), ack.size(), 0, reinterpret_cast<sockaddr*>(&src_pynq), src_len); // 按源地址原路回发 ACK

        if (sent < 0) {                    // 判断 ACK 发送是否失败
            std::cerr << "[PC] send ACK failed: " << strerror(errno) << std::endl; // 打印发送失败日志
            continue;                      // 保持服务循环，继续处理下一包
        }                                  // 结束发送失败判断

        std::cout << "[PC] send ACK ok, bytes=" << sent << std::endl; // 打印 ACK 发送成功日志
    }                                      // 结束持续循环

    close(fd_pc);                          // 关闭 socket（正常流程下不会执行到此行）
    return 0;                              // 返回成功码
}                                          // 主函数结束