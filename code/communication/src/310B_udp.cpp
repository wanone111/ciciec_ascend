#include "udp_common.h"                             // 引入 UDP 公共工具头文件
#include <iostream>                                  // 引入标准输出流
#include <vector>                                    // 引入二进制缓冲区
#include <string>                                    // 引入字符串处理
#include <sstream>                                   // 引入字符串流
#include <cstdint>                                   // 引入定长整数类型
#include <chrono>                                    // 引入时间处理
#include <thread>                                    // 引入多线程支持
#include <mutex>                                     // 引入互斥锁
#include <atomic>                                    // 引入原子操作
#include <cstring>                                   // 引入内存操作
#include <csignal>                                   // 引入信号处理
#include <sys/epoll.h>                               // epoll 事件驱动
#include <unordered_map>                             // 哈希表用于 fd 映射

using namespace udp_net;                             // 使用 udp_net 命名空间

// ============ 日志宏定义 ============
#define LOG_INFO(msg) do { \
    std::cout << "[310B] [INFO] " << msg << std::endl; \
} while(0)

#define LOG_ERROR(msg) do { \
    std::cerr << "[310B] [ERROR] " << msg << std::endl; \
} while(0)

#define LOG_DEBUG(msg) do { \
    if (g_debug_mode) { \
        std::cout << "[310B] [DEBUG] " << msg << std::endl; \
    } \
} while(0)

// ============ 全局状态标志 ============
std::atomic<bool> g_running{true};                   // 运行状态标志
bool g_debug_mode = false;                           // 调试模式开关

// ============ 视频控制命令结构体 ============
struct VideoCtrlCmd {
    std::string cmd;         // 命令类型：play/stop/pause/resume
    std::string video_path;  // 视频路径（play 命令时使用）
    uint32_t frame_id;       // 触发帧 ID
    float confidence;        // 动作置信度
    int action_type;         // 动作类型：0=未知，1=挥手，2=欢呼
};

// ============ JSON 构建函数 ============
std::string build_play_json(const std::string& video_path, uint32_t frame_id, float confidence, int action_type) {
    std::ostringstream oss;
    oss << "{\"cmd\":\"play\",\"video\":\"" << video_path 
        << "\",\"frame_id\":" << frame_id 
        << ",\"confidence\":" << confidence 
        << ",\"action_type\":" << action_type << "}";
    return oss.str();
}

std::string build_stop_json() {
    return R"({"cmd":"stop"})";
}

std::string build_pause_json() {
    return R"({"cmd":"pause"})";
}

std::string build_resume_json() {
    return R"({"cmd":"resume"})";
}

// ============ 信号处理 ============
void signal_handler(int) {
    g_running.store(false);
    std::cout << "[310B] received shutdown signal" << std::endl;
}

// ============ 姿态结果解析 ============
struct PoseResult {
    uint32_t frame_id = 0;
    uint64_t timestamp_us = 0;
    int action_flag = 0;       // 0=未知，1=挥手，2=欢呼
    float action_conf = 0.0f;
    float kps_conf_mean = 0.0f;
};

bool parse_pose_result(const std::string& json_str, PoseResult* result) {
    if (!result) return false;
    
    // 查找 action_flag
    auto pos = json_str.find("\"action_flag\":");
    if (pos != std::string::npos) {
        result->action_flag = std::stoi(json_str.substr(pos + 14));
    } else {
        return false; // 没有 action_flag 不是有效的姿态结果
    }
    
    // 查找 action_conf
    pos = json_str.find("\"action_conf\":");
    if (pos != std::string::npos) {
        result->action_conf = std::stof(json_str.substr(pos + 14));
    }
    
    // 查找 kps_conf_mean
    pos = json_str.find("\"kps_conf_mean\":");
    if (pos != std::string::npos) {
        result->kps_conf_mean = std::stof(json_str.substr(pos + 16));
    }
    
    // 查找 frame_id
    pos = json_str.find("\"frame_id\":");
    if (pos != std::string::npos) {
        result->frame_id = static_cast<uint32_t>(std::stoull(json_str.substr(pos + 11)));
    }
    
    // 查找 timestamp_ms
    pos = json_str.find("\"timestamp_ms\":");
    if (pos != std::string::npos) {
        result->timestamp_us = std::stoull(json_str.substr(pos + 15)) * 1000;
    }
    
    return true;
}

// ============ 视频联动控制器 ============
class VideoLinkageController {
public:
    VideoLinkageController(const std::string& pc_ip, int port)
        : pc_ip_(pc_ip), port_(port) {
        fd_ = create_socket();
        if (fd_ >= 0) {
            tune_socket_buffers(fd_, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES);
        }
    }
    
    ~VideoLinkageController() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    
    bool triggerAction(int action_type, float confidence, uint32_t frame_id) {
        if (fd_ < 0) return false;
        
        // 检查触发间隔，避免频繁切换
        const uint64_t now_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        
        constexpr uint64_t kMinTriggerIntervalMs = 2000; // 最小触发间隔 2 秒
        constexpr float kActionThreshold = 0.30f;        // 动作置信度阈值
        
        if (confidence < kActionThreshold) {
            return false;
        }
        
        if (now_ms - last_trigger_time_ < kMinTriggerIntervalMs && action_type == current_action_) {
            return false; // 间隔太短且是同一动作，忽略
        }
        
        // 根据动作类型选择视频路径
        std::string video_path;
        if (action_type == 1) {  // 挥手
            video_path = "/home/data/videos/wave1.mp4";
        } else if (action_type == 2) {  // 欢呼
            video_path = "/home/data/videos/heart1.mp4";
        } else {
            return false; // 未知动作
        }
        
        // 构建并发送播放指令
        std::string json_cmd = build_play_json(video_path, frame_id, confidence, action_type);
        sockaddr_in addr = make_addr(pc_ip_, port_);
        
        if (send_video_ctrl_cmd(fd_, json_cmd, addr) > 0) {
            std::cout << "[310B] [VIDEO] trigger action=" << action_type 
                      << " video=" << video_path 
                      << " conf=" << confidence << std::endl;
            
            current_action_ = action_type;
            last_trigger_time_ = now_ms;
            video_playing_ = true;
            return true;
        } else {
            std::cerr << "[310B] [VIDEO] failed to send cmd" << std::endl;
            return false;
        }
    }
    
    void stop() {
        if (fd_ >= 0 && video_playing_) {
            std::string json_cmd = build_stop_json();
            sockaddr_in addr = make_addr(pc_ip_, port_);
            send_video_ctrl_cmd(fd_, json_cmd, addr);
            video_playing_ = false;
        }
    }
    
private:
    std::string pc_ip_;
    int port_;
    int fd_ = -1;
    
    std::atomic<bool> video_playing_{false};
    int current_action_ = 0;
    uint64_t last_trigger_time_ = 0;
};

// ============ 网络统计信息 ============
struct NetworkStats {
    std::atomic<uint64_t> video_pkt_cnt{0};          // 视频包计数
    std::atomic<uint64_t> ctrl_pkt_cnt{0};           // 控制包计数
    std::atomic<uint64_t> action_trigger_cnt{0};     // 动作触发计数
    std::atomic<uint64_t> dropped_pkt_cnt{0};        // 丢包计数
    std::atomic<uint64_t> total_bytes{0};            // 总字节数
    
    void print() const {
        std::cout << "[310B] [STATS] video=" << video_pkt_cnt.load()
                  << " ctrl=" << ctrl_pkt_cnt.load()
                  << " triggers=" << action_trigger_cnt.load()
                  << " dropped=" << dropped_pkt_cnt.load()
                  << " bytes=" << total_bytes.load() << std::endl;
    }
};

// ============ 零拷贝视频转发 ============
inline int forward_video_zero_copy(int fd_in, int fd_out, 
                                   std::vector<uint8_t>& buffer,
                                   sockaddr_in& src, socklen_t& srclen,
                                   const sockaddr_in& dst,
                                   std::atomic<uint64_t>& pkt_cnt,
                                   std::atomic<uint64_t>& byte_cnt) {
    // 预分配缓冲区，避免重复分配
    if (buffer.capacity() < 2048) {
        buffer.reserve(2048);
    }
    
    int n = recv_bytes(fd_in, &buffer, &src, &srclen);
    if (n <= 0) {
        return n;
    }
    
    // 零拷贝转发：直接发送 buffer 数据
    int sent = send_bytes(fd_out, buffer.data(), n, dst);
    if (sent > 0) {
        pkt_cnt.fetch_add(1, std::memory_order_relaxed);
        byte_cnt.fetch_add(n, std::memory_order_relaxed);
    }
    return sent;
}

// ============ 源地址白名单验证 ============
bool is_valid_source(const sockaddr_in& src, const std::string& expected_ip) {
    char src_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));
    return expected_ip == src_ip;
}

// ============ 主函数 ============
int main(int argc, char* argv[]) {
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            g_debug_mode = true;
        }
    }
    
    // 注册信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    LOG_INFO("starting NPU inference and video linkage controller");
    
    // 创建三个 socket
    // 1. fd_pynq: 接收 PYNQ 的视频流（用于转发到 PC 进行渲染）
    int fd_pynq = create_socket();
    if (fd_pynq < 0) {
        return 1;
    }
    if (!bind_ip_port(fd_pynq, ASCEND_310B_IP, PYNQ_PORT)) {
        close(fd_pynq);
        return 1;
    }
    tune_socket_buffers(fd_pynq, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES);
    
    // 2. fd_pc: 与 PC 通信（接收推理结果，实际推理在 310B NPU 上进行）
    int fd_pc = create_socket();
    if (fd_pc < 0) {
        close(fd_pynq);
        return 1;
    }
    if (!bind_ip_port(fd_pc, ASCEND_310B_PC_IP, PC_CTRL_PORT)) {
        close(fd_pynq);
        close(fd_pc);
        return 1;
    }
    tune_socket_buffers(fd_pc, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES);
    
    // 3. fd_video_ctrl: 发送视频控制指令到 PC
    VideoLinkageController video_ctrl(PC_IP, PC_VIDEO_CTRL_PORT);
    
    // 初始化 epoll 事件驱动
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG_ERROR("epoll_create1 failed: " << strerror(errno));
        close(fd_pynq);
        close(fd_pc);
        return 1;
    }
    
    // 注册文件描述符到 epoll
    struct epoll_event ev_pynq{}, ev_pc{};
    ev_pynq.events = EPOLLIN;
    ev_pynq.data.fd = fd_pynq;
    ev_pc.events = EPOLLIN;
    ev_pc.data.fd = fd_pc;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_pynq, &ev_pynq) < 0) {
        LOG_ERROR("epoll_ctl add fd_pynq failed: " << strerror(errno));
        close(epoll_fd);
        close(fd_pynq);
        close(fd_pc);
        return 1;
    }
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_pc, &ev_pc) < 0) {
        LOG_ERROR("epoll_ctl add fd_pc failed: " << strerror(errno));
        close(epoll_fd);
        close(fd_pynq);
        close(fd_pc);
        return 1;
    }
    
    LOG_INFO("sockets initialized with epoll:");
    LOG_INFO("  - PYNQ video input: " << ASCEND_310B_IP << ":" << PYNQ_PORT);
    LOG_INFO("  - PC control channel: " << ASCEND_310B_PC_IP << ":" << PC_CTRL_PORT);
    LOG_INFO("  - Video ctrl output: " << PC_IP << ":" << PC_VIDEO_CTRL_PORT);
    
    // 统计信息
    NetworkStats stats;
    
    // 预分配缓冲区，避免重复分配（零拷贝优化）
    std::vector<uint8_t> video_buffer;
    std::vector<uint8_t> ctrl_buffer;
    video_buffer.reserve(2048);
    ctrl_buffer.reserve(2048);
    
    // 目标地址缓存
    sockaddr_in pc_video_addr = make_addr(PC_IP, PC_PORT);
    
    // 源地址验证
    sockaddr_in expected_pynq_addr = make_addr(PYNQ_IP, PYNQ_PORT);
    
    // epoll 事件数组
    constexpr int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
    
    // 序列号追踪（丢包检测）
    uint32_t last_video_frame_id = 0;
    uint32_t last_ctrl_frame_id = 0;
    
    // 主循环：epoll 事件驱动 + NPU 推理 + 视频联动
    while (g_running.load()) {
        // 等待事件，超时 100ms
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
        
        if (nfds < 0) {
            if (errno == EINTR) {
                continue; // 被信号中断，继续循环
            }
            LOG_ERROR("epoll_wait failed: " << strerror(errno));
            break;
        }
        
        // 处理所有就绪事件
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            
            if (fd == fd_pynq) {
                // ========== 通道 1: 接收 PYNQ 视频流并转发到 PC（零拷贝）==========
                sockaddr_in pynq_src{};
                socklen_t pynq_srclen = sizeof(pynq_src);
                
                int sent = forward_video_zero_copy(
                    fd_pynq, fd_pc, video_buffer,
                    pynq_src, pynq_srclen,
                    pc_video_addr,
                    stats.video_pkt_cnt,
                    stats.total_bytes
                );
                
                if (sent > 0) {
                    // 源地址白名单验证
                    if (!is_valid_source(pynq_src, PYNQ_IP)) {
                        LOG_DEBUG("dropped packet from invalid source");
                        stats.dropped_pkt_cnt.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    
                    // 丢包检测：检查帧序号连续性
                    VideoPacketHeader hdr{};
                    if (parse_video_header(video_buffer, &hdr)) {
                        if (last_video_frame_id > 0 && hdr.frame_id != last_video_frame_id + 1) {
                            uint32_t gap = hdr.frame_id - last_video_frame_id;
                            if (gap > 1 && gap < 1000) { // 避免初始化和回绕
                                LOG_DEBUG("video frame gap detected: " << last_video_frame_id 
                                      << " -> " << hdr.frame_id << " (lost " << (gap-1) << ")");
                                stats.dropped_pkt_cnt.fetch_add(gap - 1, std::memory_order_relaxed);
                            }
                        }
                        last_video_frame_id = hdr.frame_id;
                        
                        // 每 200 包打印一次日志
                        if (stats.video_pkt_cnt.load() % 200 == 0) {
                            LOG_INFO("[VIDEO] forwarded frame=" << hdr.frame_id
                                  << " frag=" << hdr.frag_idx << "/" << hdr.frag_cnt
                                  << " total_pkts=" << stats.video_pkt_cnt.load());
                        }
                    }
                }
            }
            else if (fd == fd_pc) {
                // ========== 通道 2: 接收 NPU 推理结果并触发视频联动 ==========
                sockaddr_in pc_src{};
                socklen_t pc_srclen = sizeof(pc_src);
                
                int n = recv_bytes(fd_pc, &ctrl_buffer, &pc_src, &pc_srclen);
                
                if (n > 0) {
                    // 源地址验证
                    if (!is_valid_source(pc_src, PC_IP)) {
                        LOG_DEBUG("dropped control packet from invalid source");
                        stats.dropped_pkt_cnt.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    
                    std::string json_str(ctrl_buffer.begin(), ctrl_buffer.end());
                    PoseResult result;
                    
                    if (parse_pose_result(json_str, &result)) {
                        stats.ctrl_pkt_cnt.fetch_add(1, std::memory_order_relaxed);
                        
                        // 丢包检测
                        if (last_ctrl_frame_id > 0 && result.frame_id != last_ctrl_frame_id + 1) {
                            uint32_t gap = result.frame_id - last_ctrl_frame_id;
                            if (gap > 1 && gap < 1000) {
                                LOG_DEBUG("ctrl frame gap: " << last_ctrl_frame_id 
                                      << " -> " << result.frame_id);
                            }
                        }
                        last_ctrl_frame_id = result.frame_id;
                        
                        LOG_DEBUG("[NPU] frame=" << result.frame_id
                              << " action=" << result.action_flag
                              << " conf=" << result.action_conf
                              << " kps=" << result.kps_conf_mean);
                        
                        // 触发动作对应的视频播放
                        if (result.action_flag != 0 && result.action_conf >= 0.30f) {
                            if (video_ctrl.triggerAction(result.action_flag, result.action_conf, result.frame_id)) {
                                stats.action_trigger_cnt.fetch_add(1, std::memory_order_relaxed);
                            }
                        }
                    }
                    
                    // 定期打印统计
                    if (stats.ctrl_pkt_cnt.load() % 50 == 0) {
                        stats.print();
                    }
                }
            }
        }
    }
    
    // 清理资源
    LOG_INFO("shutting down...");
    video_ctrl.stop();
    
    // 打印最终统计
    stats.print();
    
    close(epoll_fd);
    close(fd_pynq);
    close(fd_pc);
    
    return 0;
}