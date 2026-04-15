#include "udp_common.h"                            // 引入 UDP 公共工具头文件
#ifdef HAVE_OPENCV
#include <opencv2/opencv.hpp>
#endif
#include <iostream>                                // 引入标准输出流
#include <string>                                  // 引入字符串类型
#include <chrono>                                  // 引入时间相关工具
#include <thread>                                  // 引入线程休眠工具
#include <vector>                                  // 引入动态数组类型
#include <algorithm>                               // 引入 std::min 算法
#include <cstdlib>                                 // 引入环境变量读取接口
#include <cctype>

using namespace udp_net;                           // 使用 udp_net 命名空间，简化函数调用

#ifdef HAVE_OPENCV
static bool is_digits(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    for (unsigned char ch : s) {
        if (!std::isdigit(ch)) {
            return false;
        }
    }
    return true;
}

static bool try_open_camera_path(cv::VideoCapture& cap, const std::string& dev_path, std::string& chosen) {
    if (cap.open(dev_path, cv::CAP_V4L2)) {
        chosen = dev_path + " (CAP_V4L2)";
        return true;
    }
    cap.release();
    if (cap.open(dev_path)) {
        chosen = dev_path + " (default backend)";
        return true;
    }
    return false;
}
#endif

int main() {                                       // 主函数入口
#ifndef HAVE_OPENCV
    std::cerr << "[PYNQ] OpenCV is required for PS USB capture; rebuild with OpenCV enabled" << std::endl;
    return 1;
#else
    const char* camera_dev_env = std::getenv("UDP_CAMERA_DEV");
    std::vector<std::string> candidates;
    if (camera_dev_env) {
        std::string configured = camera_dev_env;
        // 与 Python 脚本行为一致：优先按 /dev/videoX 设备路径打开。
        if (is_digits(configured)) {
            candidates.emplace_back("/dev/video" + configured);
        } else {
            candidates.emplace_back(configured);
        }
    } else {
        candidates.emplace_back("/dev/video0");
        candidates.emplace_back("/dev/video1");
    }

    cv::VideoCapture cap;
    std::string chosen_dev;
    for (const auto& candidate : candidates) {
        if (try_open_camera_path(cap, candidate, chosen_dev)) {
            break;
        }
        cap.release();
    }
    if (!cap.isOpened()) {
        std::cerr << "[PYNQ] open camera failed; candidates=";
        for (size_t i = 0; i < candidates.size(); ++i) {
            std::cerr << candidates[i] << (i + 1 < candidates.size() ? "," : "");
        }
        std::cerr << std::endl;
        std::cerr << "[PYNQ] hint: set UDP_CAMERA_DEV=/dev/videoX (e.g. /dev/video0)" << std::endl;
        return 1;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, VIDEO_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, VIDEO_HEIGHT);
    cap.set(cv::CAP_PROP_FPS, VIDEO_FPS);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    const int got_w = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int got_h = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    const double got_fps = cap.get(cv::CAP_PROP_FPS);
    std::cout << "[PYNQ] camera opened: " << chosen_dev << " got=" << got_w << "x" << got_h << "@" << got_fps << std::endl;
#endif

    int fd_pynq = create_socket();                  // 创建 PYNQ 侧 UDP socket
    if (fd_pynq < 0) {                              // 判断 socket 是否创建失败
#ifdef HAVE_OPENCV
        cap.release();
#endif
        return 1;                                   // 返回错误码
    }                                               // 结束 socket 创建判断

    if (!bind_port(fd_pynq, PORT_PYNQ)) {           // 绑定到 PYNQ 本地端口，用于接收 PC ACK
        close(fd_pynq);                             // 绑定失败时关闭 UDP socket
#ifdef HAVE_OPENCV
        cap.release();
#endif
        return 1;                                   // 返回错误码
    }                                               // 结束端口绑定判断

    sockaddr_in pc_addr = make_addr(PC_IP, PORT_PC); // 构造 PC 目标地址结构体

    timeval timeout_cfg{};                          // 定义超时配置结构体
    timeout_cfg.tv_sec = 0;                         // 设置秒级超时为 0
    timeout_cfg.tv_usec = 20000;                    // 设置接收超时为 20ms，避免卡住发送主循环
    setsockopt(fd_pynq, SOL_SOCKET, SO_RCVTIMEO, &timeout_cfg, sizeof(timeout_cfg)); // 设置 ACK 接收超时

    uint16_t frame_id = 0;                          // 定义帧 ID，按帧递增
    uint32_t frames_sent = 0;                       // 定义发送帧计数器
    const int frame_bytes = RGB_FRAME_BYTES;        // 获取固定 RGB 帧字节数
    const uint16_t total = static_cast<uint16_t>((frame_bytes + UDP_PAYLOAD_BYTES - 1) / UDP_PAYLOAD_BYTES); // 计算固定总分片数量
    const auto frame_interval = std::chrono::microseconds(1000000 / VIDEO_FPS); // 计算单帧周期时间

    std::vector<uint8_t> packet(sizeof(UdpHeader) + UDP_PAYLOAD_BYTES); // 复用发送缓存，避免每包重复分配

    std::cout << "[PYNQ] start PS USB->UDP RGB stream " << VIDEO_WIDTH << "x" << VIDEO_HEIGHT << "@" << VIDEO_FPS << "fps" << std::endl;
    std::cout << "[PYNQ] camera device=" << chosen_dev << ", capture_fmt=BGR, send_fmt=RGB24" << std::endl;
    std::cout << "[PYNQ] chunks/frame=" << total << ", payload=" << UDP_PAYLOAD_BYTES << std::endl;

    while (true) {                                  // 进入持续发送循环
        auto loop_begin = std::chrono::steady_clock::now(); // 记录当前循环开始时间
        const uint64_t ts_us = get_timestamp_us();  // 记录该帧统一发送时间戳
        bool send_failed = false;                   // 定义当前帧发送失败标志

#ifdef HAVE_OPENCV
        cv::Mat frame_bgr;
        if (!cap.read(frame_bgr) || frame_bgr.empty()) {
            std::cerr << "[PYNQ] read camera frame failed" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        if (frame_bgr.cols != VIDEO_WIDTH || frame_bgr.rows != VIDEO_HEIGHT) {
            cv::resize(frame_bgr, frame_bgr, cv::Size(VIDEO_WIDTH, VIDEO_HEIGHT), 0.0, 0.0, cv::INTER_LINEAR);
        }

        cv::Mat frame_rgb;
        cv::cvtColor(frame_bgr, frame_rgb, cv::COLOR_BGR2RGB);
        if (!frame_rgb.isContinuous()) {
            frame_rgb = frame_rgb.clone();
        }
        const uint8_t* frame_ptr = frame_rgb.data;
#endif

        for (uint16_t seq = 0; seq < total; ++seq) { // 按分片序号循环发送该帧
            const int offset = static_cast<int>(seq) * UDP_PAYLOAD_BYTES; // 计算本分片在帧内偏移
            const uint16_t chunk_len = static_cast<uint16_t>(std::min(UDP_PAYLOAD_BYTES, frame_bytes - offset)); // 计算本分片有效载荷长度

            UdpHeader header{};                      // 定义并清零头结构
            header.frame_id = frame_id;              // 填充帧 ID 字段
            header.seq_num = seq;                    // 填充分片序号字段
            header.total = total;                    // 填充总分片数字段
            header.payload_len = chunk_len;          // 填充分片载荷长度字段
            header.timestamp = ts_us;                // 填充统一时间戳字段

            std::memcpy(packet.data(), &header, sizeof(UdpHeader)); // 将头结构拷贝到包起始位置
            std::memcpy(packet.data() + sizeof(UdpHeader), frame_ptr + offset, chunk_len); // 从 RGB 帧缓存复制当前分片载荷

            int sent = sendto(fd_pynq, packet.data(), sizeof(UdpHeader) + chunk_len, 0,
                              reinterpret_cast<const sockaddr*>(&pc_addr), sizeof(pc_addr)); // 发送当前分片
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
#ifdef HAVE_OPENCV
    cap.release();
#endif
    return 0;                                         // 返回成功码
}                                                     // 主函数结束
