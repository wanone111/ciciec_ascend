#include "udp_common.h"                    // 引入 UDP 公共工具头文件
#include <iostream>                        // 引入标准输出流
#include <string>                          // 引入字符串类型

using namespace udp_net;                   // 使用 udp_net 命名空间，简化函数调用

int main() {                               // 主函数入口
    int fd_pynq = create_socket();         // 创建一个 UDP socket，专门接收 PYNQ 消息
    if (fd_pynq < 0) {                     // 判断 socket 是否创建失败
        return 1;                          // 创建失败则返回错误码
    }                                      // 结束失败判断

    if (!bind_port(fd_pynq, PORT_PYNQ)) {  // 绑定到 PYNQ 通信端口
        close(fd_pynq);                    // 绑定失败先关闭 socket
        return 1;                          // 返回错误码
    }                                      // 结束绑定判断

    int fd_pc = create_socket();           // 创建一个 UDP socket，专门给 PC 发送并接收 ACK
    if (fd_pc < 0) {                       // 判断 socket 是否创建失败
        close(fd_pynq);                    // 关闭已创建的 PYNQ socket
        return 1;                          // 返回错误码
    }                                      // 结束失败判断

    if (!bind_port(fd_pc, PORT_PC)) {      // 绑定到 310B 的 PC 通信端口，便于接收 PC 的 ACK
        close(fd_pynq);                    // 关闭 PYNQ socket
        close(fd_pc);                      // 关闭 PC socket
        return 1;                          // 返回错误码
    }                                      // 结束绑定判断

    const std::string pc_ip = "PC_IP";     // 目标 PC 的 IP 占位，后续替换成真实 IP
    sockaddr_in pc_addr = make_addr(pc_ip, PORT_PC); // 生成 PC 目标地址结构体

    std::cout << "[310B] wait message from PYNQ..." << std::endl; // 打印等待提示

    sockaddr_in pynq_src{};                // 定义结构体保存 PYNQ 源地址
    socklen_t pynq_src_len = sizeof(pynq_src); // 定义并初始化源地址长度

    std::string msg_from_pynq = recv_msg(fd_pynq, &pynq_src, &pynq_src_len); // 阻塞接收 PYNQ 消息
    if (msg_from_pynq.empty()) {           // 判断是否接收失败或收到空消息
        std::cerr << "[310B] recv from PYNQ failed" << std::endl; // 打印错误信息
        close(fd_pynq);                    // 关闭 PYNQ socket
        close(fd_pc);                      // 关闭 PC socket
        return 1;                          // 返回错误码
    }                                      // 结束接收判断

    std::cout << "[310B] recv from PYNQ: " << msg_from_pynq << std::endl; // 打印收到的 PYNQ 内容

    std::string to_pc = "RESULT_FROM_310B:" + msg_from_pynq; // 拼接发给 PC 的验证消息
    int sent = send_msg(fd_pc, to_pc, pc_addr); // 向 PC 发送验证消息
    if (sent < 0) {                        // 判断发送是否失败
        std::cerr << "[310B] send to PC failed" << std::endl; // 打印错误日志
        close(fd_pynq);                    // 关闭 PYNQ socket
        close(fd_pc);                      // 关闭 PC socket
        return 1;                          // 返回错误码
    }                                      // 结束发送判断

    std::cout << "[310B] send to PC ok, bytes=" << sent << std::endl; // 打印发送成功日志

    std::cout << "[310B] wait ACK from PC..." << std::endl; // 打印等待 ACK 提示
    sockaddr_in pc_src{};                  // 定义结构体保存 PC 源地址
    socklen_t pc_src_len = sizeof(pc_src); // 初始化源地址长度
    std::string ack = recv_msg(fd_pc, &pc_src, &pc_src_len); // 在 PC 端口阻塞接收 ACK

    if (ack.empty()) {                     // 判断 ACK 是否接收失败
        std::cerr << "[310B] recv ACK failed" << std::endl; // 打印错误日志
        close(fd_pynq);                    // 关闭 PYNQ socket
        close(fd_pc);                      // 关闭 PC socket
        return 1;                          // 返回错误码
    }                                      // 结束 ACK 判断

    std::cout << "[310B] recv ACK from PC: " << ack << std::endl; // 打印 ACK 内容

    close(fd_pynq);                        // 正常结束前关闭 PYNQ socket
    close(fd_pc);                          // 正常结束前关闭 PC socket
    return 0;                              // 返回成功码
}                                          // 主函数结束