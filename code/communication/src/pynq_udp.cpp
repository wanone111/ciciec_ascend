#include "udp_common.h"                            // 引入 UDP 公共工具头文件
#include <iostream>                                // 引入标准输出流
#include <string>                                  // 引入字符串类型
#include <chrono>                                  // 引入时间相关工具
#include <thread>                                  // 引入线程休眠工具
#include <vector>                                  // 引入动态数组类型
#include <algorithm>                               // 引入 std::min 算法
#include <cstdlib>                                 // 引入环境变量读取接口
#include <fcntl.h>                                 // 引入 open 相关常量与函数
#include <sys/mman.h>                              // 引入 mmap 相关接口
#include <sys/stat.h>                              // 引入 fstat 相关接口

using namespace udp_net;                           // 使用 udp_net 命名空间，简化函数调用

int main() {                                       // 主函数入口
    const char* dma_dev_env = std::getenv("UDP_DMA_DEV"); // 读取 DMA 设备路径环境变量
    const std::string dma_dev = dma_dev_env ? dma_dev_env : "/dev/udmabuf0"; // 设置 DMA 设备默认路径

    int dma_fd = open(dma_dev.c_str(), O_RDONLY);  // 以只读方式打开 DMA 缓冲设备
    if (dma_fd < 0) {                               // 判断 DMA 设备是否打开失败
        std::cerr << "[PYNQ] open dma device failed: " << dma_dev << ", err=" << strerror(errno) << std::endl; // 打印 DMA 打开失败日志
        return 1;                                   // 返回错误码
    }                                               // 结束 DMA 设备打开判断

    struct stat dma_stat{};                         // 定义结构体保存设备文件状态
    if (fstat(dma_fd, &dma_stat) != 0) {            // 读取 DMA 设备文件状态
        std::cerr << "[PYNQ] fstat dma device failed: " << strerror(errno) << std::endl; // 打印 fstat 失败日志
        close(dma_fd);                              // 关闭 DMA 设备文件
        return 1;                                   // 返回错误码
    }                                               // 结束 fstat 判断

    if (dma_stat.st_size < NV12_FRAME_BYTES) {      // 判断 DMA 缓冲区大小是否足够容纳一帧 NV12
        std::cerr << "[PYNQ] dma buffer too small, size=" << dma_stat.st_size << ", need=" << NV12_FRAME_BYTES << std::endl; // 打印缓冲区不足日志
        close(dma_fd);                              // 关闭 DMA 设备文件
        return 1;                                   // 返回错误码
    }                                               // 结束缓冲区大小判断

    void* dma_map = mmap(nullptr, dma_stat.st_size, PROT_READ, MAP_SHARED, dma_fd, 0); // 将 DMA 缓冲区映射到用户空间
    if (dma_map == MAP_FAILED) {                    // 判断 mmap 是否失败
        std::cerr << "[PYNQ] mmap dma buffer failed: " << strerror(errno) << std::endl; // 打印 mmap 失败日志
        close(dma_fd);                              // 关闭 DMA 设备文件
        return 1;                                   // 返回错误码
    }                                               // 结束 mmap 判断

    const uint8_t* dma_frame_ptr = reinterpret_cast<const uint8_t*>(dma_map); // 获取 DMA 缓冲区起始指针

    int fd_pynq = create_socket();                  // 创建 PYNQ 侧 UDP socket
    if (fd_pynq < 0) {                              // 判断 socket 是否创建失败
        munmap(dma_map, dma_stat.st_size);          // 释放 DMA 映射
        close(dma_fd);                              // 关闭 DMA 设备文件
        return 1;                                   // 返回错误码
    }                                               // 结束 socket 创建判断

    if (!bind_port(fd_pynq, PORT_PYNQ)) {           // 绑定到 PYNQ 本地端口，用于接收 PC ACK
        close(fd_pynq);                             // 绑定失败时关闭 UDP socket
        munmap(dma_map, dma_stat.st_size);          // 释放 DMA 映射
        close(dma_fd);                              // 关闭 DMA 设备文件
        return 1;                                   // 返回错误码
    }                                               // 结束端口绑定判断

    sockaddr_in pc_addr = make_addr(PC_IP, PORT_PC); // 构造 PC 目标地址结构体

    timeval timeout_cfg{};                          // 定义超时配置结构体
    timeout_cfg.tv_sec = 0;                         // 设置秒级超时为 0
    timeout_cfg.tv_usec = 20000;                    // 设置接收超时为 20ms，避免卡住 60fps 主循环
    setsockopt(fd_pynq, SOL_SOCKET, SO_RCVTIMEO, &timeout_cfg, sizeof(timeout_cfg)); // 设置 ACK 接收超时

    uint16_t frame_id = 0;                          // 定义帧 ID，按帧递增
    uint32_t frames_sent = 0;                       // 定义发送帧计数器
    const int frame_bytes = NV12_FRAME_BYTES;       // 获取固定 NV12 帧字节数
    const uint16_t total = static_cast<uint16_t>((frame_bytes + UDP_PAYLOAD_BYTES - 1) / UDP_PAYLOAD_BYTES); // 计算固定总分片数量
    const auto frame_interval = std::chrono::microseconds(1000000 / VIDEO_FPS); // 计算单帧周期时间

    std::cout << "[PYNQ] start DMA->UDP NV12 stream 1280x960@60fps" << std::endl; // 打印 DMA 视频流启动信息
    std::cout << "[PYNQ] dma device=" << dma_dev << ", chunks/frame=" << total << std::endl; // 打印 DMA 设备与分片信息

    while (true) {                                  // 进入持续发送循环
        auto loop_begin = std::chrono::steady_clock::now(); // 记录当前循环开始时间
        const uint64_t ts_us = get_timestamp_us();  // 记录该帧统一发送时间戳
        bool send_failed = false;                   // 定义当前帧发送失败标志

        for (uint16_t seq = 0; seq < total; ++seq) { // 按分片序号循环发送该帧
            const int offset = static_cast<int>(seq) * UDP_PAYLOAD_BYTES; // 计算本分片在帧内偏移
            const uint16_t chunk_len = static_cast<uint16_t>(std::min(UDP_PAYLOAD_BYTES, frame_bytes - offset)); // 计算本分片有效载荷长度

            UdpHeader header{};                      // 定义并清零头结构
            header.frame_id = frame_id;              // 填充帧 ID 字段
            header.seq_num = seq;                    // 填充分片序号字段
            header.total = total;                    // 填充总分片数字段
            header.payload_len = chunk_len;          // 填充分片载荷长度字段
            header.timestamp = ts_us;                // 填充统一时间戳字段

            std::vector<uint8_t> packet(sizeof(UdpHeader) + chunk_len); // 分配完整 UDP 包缓存（头 + 载荷）
            std::memcpy(packet.data(), &header, sizeof(UdpHeader)); // 将头结构拷贝到包起始位置
            std::memcpy(packet.data() + sizeof(UdpHeader), dma_frame_ptr + offset, chunk_len); // 从 DMA 缓冲区复制当前分片载荷

            int sent = sendto(fd_pynq, packet.data(), packet.size(), 0, reinterpret_cast<const sockaddr*>(&pc_addr), sizeof(pc_addr)); // 发送当前分片
            if (sent < 0) {                          // 判断分片发送是否失败
                std::cerr << "[PYNQ] send chunk failed, frame=" << frame_id << ", seq=" << seq << std::endl; // 打印分片发送失败日志
                send_failed = true;                  // 标记该帧发送失败
                break;                               // 终止该帧剩余分片发送
            }                                        // 结束发送失败判断
        }                                            // 结束分片发送循环

        if (!send_failed) {                         // 若该帧完整发送成功
            ++frames_sent;                           // 增加发送帧计数

            if (frames_sent % ACK_EVERY_N_FRAMES == 0) { // 每 30 帧执行一次 ACK 接收
                sockaddr_in src_pc{};                // 定义结构体保存 ACK 来源地址
                socklen_t src_len = sizeof(src_pc);  // 初始化来源地址长度
                std::string ack = recv_msg(fd_pynq, &src_pc, &src_len); // 尝试接收 PC 返回的 ACK 文本
                if (!ack.empty()) {                  // 判断 ACK 是否接收成功
                    std::cout << "[PYNQ] recv ACK: " << ack << std::endl; // 打印 ACK 成功日志
                } else {                             // 进入 ACK 超时或失败分支
                    std::cout << "[PYNQ] ACK timeout on frame " << frame_id << std::endl; // 打印 ACK 超时日志
                }                                    // 结束 ACK 判断
            }                                        // 结束 ACK 周期判断

            if (frames_sent % 60 == 0) {            // 每发送 60 帧打印一次统计日志
                std::cout << "[PYNQ] sent frames=" << frames_sent << ", last frame_id=" << frame_id << std::endl; // 打印发送统计信息
            }                                        // 结束统计打印判断
        }                                            // 结束完整发送成功分支

        ++frame_id;                                  // 帧 ID 自增（uint16_t 溢出后自然回绕）

        auto elapsed = std::chrono::steady_clock::now() - loop_begin; // 计算当前循环已耗时
        if (elapsed < frame_interval) {              // 判断是否需要节拍等待
            std::this_thread::sleep_for(frame_interval - elapsed); // 通过休眠补齐到目标帧间隔
        }                                             // 结束节拍等待判断
    }                                                 // 结束持续发送循环

    close(fd_pynq);                                   // 关闭 socket（正常流程下不会执行）
    munmap(dma_map, dma_stat.st_size);                // 释放 DMA 映射（正常流程下不会执行）
    close(dma_fd);                                    // 关闭 DMA 设备（正常流程下不会执行）
    return 0;                                         // 返回成功码
}                                                     // 主函数结束
