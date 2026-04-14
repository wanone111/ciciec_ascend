#include "udp_common.h"                              // 引入 UDP 公共工具头文件
#ifdef HAVE_OPENCV                                    // 若构建时启用了 OpenCV
#include <opencv2/opencv.hpp>                        // 引入 OpenCV 图像显示与颜色转换接口
#endif                                               // 结束 OpenCV 条件包含
#include <iostream>                                  // 引入标准输出流
#include <string>                                    // 引入字符串类型
#include <vector>                                    // 引入动态数组容器
#include <cstdlib>                                   // 引入环境变量读取接口

using namespace udp_net;                             // 使用 udp_net 命名空间，简化函数调用

int main() {                                         // 主函数入口
    int fd_pc = create_socket();                     // 创建 PC 侧 UDP socket
    if (fd_pc < 0) {                                 // 判断创建是否失败
        return 1;                                    // 创建失败返回错误码
    }                                                // 结束创建失败判断

    if (!bind_port(fd_pc, PORT_PC)) {                // 绑定到 PC 通信端口，接收 PYNQ 视频分片
        close(fd_pc);                                // 绑定失败关闭 socket
        return 1;                                    // 返回错误码
    }                                                // 结束绑定失败判断

    bool show_window = (std::getenv("UDP_SHOW") != nullptr); // 通过环境变量 UDP_SHOW 控制是否显示画面
#ifndef HAVE_OPENCV                                  // 若未启用 OpenCV
    if (show_window) {                               // 判断用户是否要求显示窗口
        std::cout << "[PC] UDP_SHOW is set, but OpenCV is disabled at build time" << std::endl; // 打印提示日志
    }                                                // 结束提示分支
    show_window = false;                             // 未启用 OpenCV 时强制关闭显示
#endif                                               // 结束 OpenCV 条件分支
    if (show_window) {                               // 判断是否启用显示窗口
#ifdef HAVE_OPENCV                                  // 若构建时启用了 OpenCV
        cv::namedWindow("pc_nv12_stream", cv::WINDOW_NORMAL);        // 创建可缩放显示窗口
#endif                                               // 结束 OpenCV 窗口创建分支
    }                                                // 结束窗口创建判断

    uint16_t active_frame_id = 0;                   // 记录当前正在重组的帧 ID
    uint16_t active_total = 0;                      // 记录当前帧总分片数
    uint64_t active_ts = 0;                         // 记录当前帧时间戳
    bool has_active_frame = false;                  // 标记当前是否存在正在重组的帧

    std::vector<std::vector<uint8_t>> chunks;       // 保存当前帧每个分片的载荷数据
    std::vector<bool> received_flags;               // 标记当前帧每个分片是否已收到
    uint16_t received_count = 0;                    // 记录当前帧已收到分片数
    uint32_t complete_frames = 0;                   // 记录完整接收帧数
    uint32_t dropped_frames = 0;                    // 记录丢弃帧数（缺包或异常）

    std::cout << "[PC] start NV12 receiver 1280x960@60fps" << std::endl; // 打印接收端启动日志

    while (true) {                                   // 进入持续接收循环
        char recv_buf[BUF_SIZE];                     // 定义单次接收缓冲区
        sockaddr_in src_pynq{};                      // 定义结构体保存 PYNQ 源地址
        socklen_t src_len = sizeof(src_pynq);        // 初始化源地址长度

        int n = recvfrom(fd_pc, recv_buf, sizeof(recv_buf), 0, reinterpret_cast<sockaddr*>(&src_pynq), &src_len); // 接收一条 UDP 分片数据
        if (n <= 0) {                                // 判断接收是否失败
            if (n < 0) {                             // 判断是否为错误返回
                std::cerr << "[PC] recvfrom failed: " << strerror(errno) << std::endl; // 打印接收失败日志
            }                                        // 结束错误返回判断
            continue;                                // 接收失败时继续下一轮循环
        }                                            // 结束接收失败判断

        if (n < static_cast<int>(sizeof(UdpHeader))) { // 判断包长度是否至少容纳头结构
            std::cerr << "[PC] drop tiny packet, bytes=" << n << std::endl; // 打印短包丢弃日志
            continue;                                 // 丢弃非法短包
        }                                             // 结束短包判断

        UdpHeader header{};                           // 定义头结构实例
        std::memcpy(&header, recv_buf, sizeof(UdpHeader)); // 从接收缓冲区拷贝头结构

        const int payload_bytes = n - static_cast<int>(sizeof(UdpHeader)); // 计算当前 UDP 包的实际载荷字节数
        if (header.total == 0 || payload_bytes <= 0) { // 判断头字段是否非法
            std::cerr << "[PC] drop invalid header, total=" << header.total << ", payload=" << payload_bytes << std::endl; // 打印头字段异常日志
            continue;                                  // 丢弃非法包
        }                                              // 结束头字段合法性判断

        if (header.seq_num >= header.total) {          // 判断分片序号是否越界
            std::cerr << "[PC] drop invalid seq, seq=" << header.seq_num << ", total=" << header.total << std::endl; // 打印序号越界日志
            continue;                                  // 丢弃序号越界包
        }                                              // 结束分片序号判断

        if (header.payload_len != payload_bytes) {     // 判断头中 payload_len 与实际包长度是否一致
            std::cerr << "[PC] drop len mismatch, header=" << header.payload_len << ", actual=" << payload_bytes << std::endl; // 打印长度不一致日志
            continue;                                  // 丢弃长度不一致包
        }                                              // 结束长度一致性判断

        if (!has_active_frame || header.frame_id != active_frame_id) { // 判断是否需要切换到新帧
            if (has_active_frame && received_count < active_total) {     // 若旧帧未收完整就遇到新帧
                ++dropped_frames;                                        // 统计旧帧丢弃次数
                std::cout << "[PC] drop incomplete frame id=" << active_frame_id << ", recv=" << received_count << "/" << active_total << std::endl; // 打印丢帧日志
            }                                                            // 结束旧帧丢弃判断

            active_frame_id = header.frame_id;                           // 切换当前活动帧 ID
            active_total = header.total;                                 // 记录新帧总分片数
            active_ts = header.timestamp;                                // 记录新帧时间戳
            has_active_frame = true;                                     // 标记当前存在活动帧

            chunks.assign(active_total, std::vector<uint8_t>());         // 按总分片数重建载荷容器
            received_flags.assign(active_total, false);                  // 初始化每个分片的接收标记为 false
            received_count = 0;                                          // 重置已接收分片计数
        }                                                                // 结束新帧切换判断

        if (!received_flags[header.seq_num]) {                           // 判断当前分片是否首次到达
            chunks[header.seq_num].assign(                               // 保存当前分片的载荷数据
                reinterpret_cast<const uint8_t*>(recv_buf + sizeof(UdpHeader)),
                reinterpret_cast<const uint8_t*>(recv_buf + sizeof(UdpHeader) + payload_bytes)
            );                                                           // 结束分片数据保存
            received_flags[header.seq_num] = true;                      // 将当前分片标记为已接收
            ++received_count;                                            // 增加已接收分片计数
        }                                                                // 结束重复分片判断

        if (received_count == active_total) {                            // 判断当前帧是否已经收齐所有分片
            std::vector<uint8_t> nv12_frame;                             // 定义重组后 NV12 帧缓存
            nv12_frame.reserve(NV12_FRAME_BYTES);                        // 预留固定帧大小，减少内存扩容次数

            for (uint16_t i = 0; i < active_total; ++i) {                // 按序拼接每个分片数据
                nv12_frame.insert(nv12_frame.end(), chunks[i].begin(), chunks[i].end()); // 将分片数据追加到完整帧缓存
            }                                                             // 结束分片拼接循环

            if (static_cast<int>(nv12_frame.size()) != NV12_FRAME_BYTES) { // 判断重组帧大小是否符合 NV12 固定长度
                ++dropped_frames;                                         // 统计帧大小异常导致的丢帧
                std::cerr << "[PC] drop bad frame size, frame=" << active_frame_id << ", size=" << nv12_frame.size() << std::endl; // 打印帧大小异常日志
                has_active_frame = false;                                 // 清空活动帧状态，等待下一帧
                continue;                                                 // 跳过后续处理流程
            }                                                             // 结束帧大小合法性判断

            ++complete_frames;                                            // 增加完整接收帧统计计数

            if (show_window) {                                            // 判断是否启用可视化显示
#ifdef HAVE_OPENCV                                                        // 若构建时启用了 OpenCV
                cv::Mat nv12_mat(VIDEO_HEIGHT * 3 / 2, VIDEO_WIDTH, CV_8UC1, nv12_frame.data()); // 使用 NV12 内存构造 Mat 视图
                cv::Mat bgr;                                              // 定义 BGR 显示图像容器
                cv::cvtColor(nv12_mat, bgr, cv::COLOR_YUV2BGR_NV12);     // 将 NV12 转换为 BGR 便于显示
                cv::imshow("pc_nv12_stream", bgr);                       // 显示当前完整帧
                cv::waitKey(1);                                           // 处理窗口事件并刷新显示
#endif                                                                    // 结束 OpenCV 显示分支
            }                                                             // 结束显示分支

            if (complete_frames % ACK_EVERY_N_FRAMES == 0) {              // 每 30 帧回发一次 ACK
                std::string ack = "ACK_FROM_PC frame=" + std::to_string(active_frame_id); // 构造 ACK 文本并带上帧号
                int sent = sendto(fd_pc, ack.c_str(), ack.size(), 0, reinterpret_cast<sockaddr*>(&src_pynq), src_len); // 将 ACK 回发到发送方地址
                if (sent < 0) {                                           // 判断 ACK 发送是否失败
                    std::cerr << "[PC] send ACK failed: " << strerror(errno) << std::endl; // 打印 ACK 发送失败日志
                } else {                                                  // 进入 ACK 发送成功分支
                    std::cout << "[PC] send ACK ok, frame=" << active_frame_id << std::endl; // 打印 ACK 发送成功日志
                }                                                         // 结束 ACK 发送结果判断
            }                                                             // 结束 ACK 周期判断

            if (complete_frames % 60 == 0) {                              // 每收满 60 帧打印一次统计日志
                const uint64_t now_us = get_timestamp_us();               // 获取当前时间戳用于延迟估算
                const uint64_t one_way_us = (now_us > active_ts) ? (now_us - active_ts) : 0; // 估算单向延迟（同主机时钟域下有效）
                std::cout << "[PC] complete=" << complete_frames << ", dropped=" << dropped_frames << ", last_frame=" << active_frame_id << ", est_one_way_ms=" << (one_way_us / 1000.0) << std::endl; // 打印周期统计日志
            }                                                             // 结束统计日志判断

            has_active_frame = false;                                     // 清空活动帧状态，准备接收下一帧
        }                                                                  // 结束整帧收齐判断
    }                                                                      // 结束持续接收循环

    close(fd_pc);                                                          // 关闭 socket（正常流程下不会执行到此行）
    return 0;                                                              // 返回成功码
}                                                                          // 主函数结束