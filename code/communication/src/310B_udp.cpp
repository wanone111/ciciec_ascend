#include "udp_common.h"                             // 引入 UDP 公共工具头文件
#include <iostream>                                  // 引入标准输出流
#include <vector>                                    // 引入二进制缓冲区
#include <string>                                    // 引入字符串处理
#include <cstdint>                                   // 引入定长整数类型
#include <chrono>                                    // 引入时间处理
#include <thread>                                    // 引入多线程支持
#include <mutex>                                     // 引入互斥锁
#include <atomic>                                    // 引入原子操作
#include <cstring>                                   // 引入内存操作
#include <csignal>                                   // 引入信号处理
#include <unordered_map>                             // 哈希表用于 fd 映射
#include <functional>                                // 引入 std::function 用于回调
#include <queue>                                     // 引入队列用于批量处理
#include <memory>                                    // 引入智能指针

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

// ============ 优化的 UdpSender 类：预连接 socket，自动错误恢复 ============
class UdpSender {
public:
    explicit UdpSender(const std::string& dst_ip, int dst_port,
                       int sndbuf_bytes = DEFAULT_UDP_SNDBUF_BYTES)
        : fd_(-1), dst_addr_(make_addr(dst_ip, dst_port)), 
          sndbuf_bytes_(sndbuf_bytes), connected_(false),
          error_count_(0), send_count_(0), byte_count_(0), 
          error_total_(0), reconnect_count_(0) {
        fd_ = create_socket();
        if (fd_ >= 0) {
            tune_socket_buffers(fd_, 0, sndbuf_bytes_);
            // 预连接目标地址，后续可使用 write() 替代 sendto()
            if (::connect(fd_, reinterpret_cast<const sockaddr*>(&dst_addr_), sizeof(dst_addr_)) == 0) {
                connected_ = true;
            }
        }
    }

    ~UdpSender() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    // 发送数据（零拷贝）
    bool send(const uint8_t* data, size_t size) {
        if (fd_ < 0 || !data || size == 0) {
            return false;
        }

        int sent = 0;
        if (connected_) {
            // 已连接状态使用 write()，减少系统调用开销
            sent = static_cast<int>(write(fd_, data, size));
        } else {
            // 未连接状态使用 sendto()
            sent = send_bytes(fd_, data, size, dst_addr_);
        }

        if (sent < 0) {
            error_count_++;
            error_total_++;
            // 自动重连机制
            if (error_count_ >= 10) {
                reconnect();
            }
            return false;
        }

        error_count_ = 0;  // 重置错误计数
        send_count_++;
        byte_count_ += sent;
        return true;
    }

    // 发送 vector 数据
    bool send(const std::vector<uint8_t>& data) {
        return send(data.data(), data.size());
    }

    // 发送字符串
    bool send(const std::string& data) {
        return send(reinterpret_cast<const uint8_t*>(data.c_str()), data.size());
    }

    // 获取统计信息
    struct Stats {
        uint64_t send_count = 0;
        uint64_t byte_count = 0;
        uint64_t error_count = 0;
        uint64_t reconnect_count = 0;
    };

    Stats get_stats() const {
        Stats s;
        s.send_count = send_count_.load();
        s.byte_count = byte_count_.load();
        s.error_count = error_total_.load();
        s.reconnect_count = reconnect_count_.load();
        return s;
    }

    // 重置统计
    void reset_stats() {
        send_count_ = 0;
        byte_count_ = 0;
        error_count_ = 0;
        error_total_ = 0;
        reconnect_count_ = 0;
    }

private:
    void reconnect() {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = create_socket();
        if (fd_ >= 0) {
            tune_socket_buffers(fd_, 0, sndbuf_bytes_);
            if (::connect(fd_, reinterpret_cast<const sockaddr*>(&dst_addr_), sizeof(dst_addr_)) == 0) {
                connected_ = true;
                reconnect_count_++;
                error_count_ = 0;
            }
        }
    }

    int fd_;
    sockaddr_in dst_addr_;
    int sndbuf_bytes_;
    bool connected_;
    std::atomic<int> error_count_;
    std::atomic<uint64_t> send_count_;
    std::atomic<uint64_t> byte_count_;
    std::atomic<uint64_t> error_total_;
    std::atomic<uint64_t> reconnect_count_;
};

// ============ 完整视频帧结构 ============
struct CompleteVideoFrame {
    uint32_t frame_id = 0;                           // 帧序号
    uint64_t timestamp_us = 0;                       // 时间戳（微秒）
    uint16_t width = 0;                              // 视频宽度
    uint16_t height = 0;                             // 视频高度
    uint8_t pixel_format = 0;                        // 像素格式
    std::vector<uint8_t> data;                       // 完整帧数据
};

// ============ 视频流接口类（供 Python 调用）============
class VideoStreamInterface {
public:
    using FrameCallback = std::function<void(const CompleteVideoFrame&)>;

    VideoStreamInterface(int listen_port = PYNQ_PORT, FrameCallback callback = nullptr)
        : listen_port_(listen_port), frame_callback_(callback), fd_(-1) {
        // 使用 EpollManager 统一管理 socket
        epoll_manager_ = std::make_shared<EpollManager>(64);
        
        fd_ = create_socket();
        if (fd_ >= 0) {
            tune_socket_buffers(fd_, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES);
            if (!bind_ip_port(fd_, ASCEND_310B_IP, listen_port_)) {
                close(fd_);
                fd_ = -1;
                return;
            }
            // 添加到 epoll 监听
            epoll_manager_->add_socket(fd_, [this](int fd) {
                handleSocketEvent(fd);
            });
        }
    }

    ~VideoStreamInterface() {
        stop();
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    // 设置帧回调函数
    void setFrameCallback(FrameCallback callback) {
        frame_callback_ = callback;
    }

    // 启动接收线程（后台运行 epoll）
    void start() {
        if (fd_ < 0) {
            LOG_ERROR("socket not initialized");
            return;
        }

        LOG_INFO("video stream interface started on port " << listen_port_);
        // 启动 epoll 循环（阻塞模式）
        epoll_manager_->start();
    }

    // 后台启动
    void start_background() {
        if (fd_ < 0) {
            LOG_ERROR("socket not initialized");
            return;
        }
        
        LOG_INFO("video stream interface started on port " << listen_port_);
        // 后台启动 epoll 循环
        epoll_manager_->start_background();
    }

    // 停止
    void stop() {
        g_running.store(false);
        if (epoll_manager_) {
            epoll_manager_->stop();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // 获取详细统计信息
    std::unordered_map<std::string, uint64_t> get_detailed_stats() const {
        std::unordered_map<std::string, uint64_t> stats;
        stats["frame_cnt"] = stats_.frame_cnt.load();
        stats["pkt_cnt"] = stats_.pkt_cnt.load();
        stats["dropped_pkt_cnt"] = stats_.dropped_pkt_cnt.load();
        stats["dropped_frame_cnt"] = stats_.dropped_frame_cnt.load();
        stats["total_bytes"] = stats_.total_bytes.load();
        stats["result_send_cnt"] = stats_.result_send_cnt.load();
        stats["result_send_bytes"] = stats_.result_send_bytes.load();
        
        // 计算丢包率
        uint64_t total_pkt = stats_.pkt_cnt.load() + stats_.dropped_pkt_cnt.load();
        if (total_pkt > 0) {
            stats["drop_rate_per_mille"] = (stats_.dropped_pkt_cnt.load() * 1000) / total_pkt;
        } else {
            stats["drop_rate_per_mille"] = 0;
        }
        
        // 延迟统计
        stats["min_latency_us"] = min_latency_us_.load();
        stats["max_latency_us"] = max_latency_us_.load();
        uint64_t avg_count = avg_latency_count_.load();
        if (avg_count > 0) {
            stats["avg_latency_us"] = total_latency_us_.load() / avg_count;
        } else {
            stats["avg_latency_us"] = 0;
        }
        
        return stats;
    }

    // 打印统计信息
    void printStats() const {
        auto stats = get_detailed_stats();
        std::cout << "[310B] [STATS] frames=" << stats["frame_cnt"]
                  << " pkts=" << stats["pkt_cnt"]
                  << " drop_pkt=" << stats["dropped_pkt_cnt"]
                  << " drop_frame=" << stats["dropped_frame_cnt"]
                  << " bytes=" << stats["total_bytes"]
                  << " result_send=" << stats["result_send_cnt"]
                  << " result_bytes=" << stats["result_send_bytes"]
                  << " drop_rate=\" << (stats[\"drop_rate_per_mille\"] / 10.0) << \"%"
                  << " latency(min/max/avg)=" << stats["min_latency_us"] << "/" 
                  << stats["max_latency_us"] << "/" << stats["avg_latency_us"] << "us"
                  << std::endl;
    }

private:
    // Socket 事件处理函数（由 epoll 回调）
    void handleSocketEvent(int fd) {
        std::vector<uint8_t> buffer;
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);

        int n = recv_bytes(fd, &buffer, &src, &srclen);
        if (n <= 0) {
            return;
        }

        // 解析包头
        VideoPacketHeader hdr{};
        if (!parse_video_header(buffer, &hdr)) {
            LOG_DEBUG("invalid video packet header");
            stats_.dropped_pkt_cnt.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // 获取或创建组装器
        auto it = assemblers_.find(hdr.frame_id);
        if (it == assemblers_.end()) {
            // 清理超时的旧帧
            uint64_t now_ms = get_time_ms();
            for (auto fit = assemblers_.begin(); fit != assemblers_.end();) {
                if (now_ms - fit->second.last_update_ms > timeout_ms_) {
                    stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
                    fit = assemblers_.erase(fit);
                } else {
                    ++fit;
                }
            }

            // 如果太多帧，移除最旧的
            if (assemblers_.size() >= MAX_FRAMES_) {
                auto oldest = assemblers_.begin();
                for (auto it_old = assemblers_.begin(); it_old != assemblers_.end(); ++it_old) {
                    if (it_old->second.last_update_ms < oldest->second.last_update_ms) {
                        oldest = it_old;
                    }
                }
                stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
                assemblers_.erase(oldest);
            }

            assemblers_[hdr.frame_id] = FrameAssembler(hdr);
            it = assemblers_.find(hdr.frame_id);
        }

        FrameAssembler& assembler = it->second;

        if (hdr.frag_cnt != assembler.frag_cnt) {
            stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
            assemblers_.erase(it);
            return;
        }
        if (hdr.frag_idx + 1 < hdr.frag_cnt && hdr.payload_bytes != UDP_SAFE_PAYLOAD) {
            stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
            assemblers_.erase(it);
            return;
        }

        // 添加分片
        if (assembler.addFragment(hdr.frag_idx, hdr.frag_cnt,
                                  buffer.data() + sizeof(VideoPacketHeader),
                                  hdr.payload_bytes)) {
            // 帧组装完成
            uint64_t now_us = get_time_us();
            uint64_t latency_us = now_us - hdr.timestamp_us;
            
            // 更新延迟统计
            update_latency_stats(latency_us);

            CompleteVideoFrame frame;
            frame.frame_id = hdr.frame_id;
            frame.timestamp_us = hdr.timestamp_us;
            frame.width = hdr.width;
            frame.height = hdr.height;
            frame.pixel_format = hdr.pixel_format;
            frame.data = std::move(assembler.data);

            stats_.frame_cnt.fetch_add(1, std::memory_order_relaxed);
            stats_.total_bytes.fetch_add(frame.data.size(), std::memory_order_relaxed);

            // 调用回调
            if (frame_callback_) {
                frame_callback_(frame);
            }

            // 移除已完成的帧
            assemblers_.erase(it);
        }

        stats_.pkt_cnt.fetch_add(1, std::memory_order_relaxed);

        // 定期打印统计
        if (stats_.pkt_cnt.load() % 500 == 0) {
            printStats();
        }
    }

    // 更新延迟统计
    void update_latency_stats(uint64_t latency_us) {
        uint64_t current_min = min_latency_us_.load(std::memory_order_relaxed);
        while (latency_us < current_min && 
               !min_latency_us_.compare_exchange_weak(current_min, latency_us, 
                                                       std::memory_order_relaxed));
        
        uint64_t current_max = max_latency_us_.load(std::memory_order_relaxed);
        while (latency_us > current_max && 
               !max_latency_us_.compare_exchange_weak(current_max, latency_us, 
                                                       std::memory_order_relaxed));
        
        total_latency_us_.fetch_add(latency_us, std::memory_order_relaxed);
        avg_latency_count_.fetch_add(1, std::memory_order_relaxed);
    }

    struct FrameAssembler {
        std::vector<uint8_t> data;
        std::vector<uint8_t> frag_recv;
        std::vector<uint16_t> frag_sizes;
        uint16_t frag_cnt = 0;
        uint16_t recv_cnt = 0;
        std::size_t total_bytes = 0;
        uint64_t last_update_ms = 0;
        bool complete = false;

        FrameAssembler() : last_update_ms(get_time_ms()) {}
        explicit FrameAssembler(const VideoPacketHeader& hdr)
            : frag_cnt(hdr.frag_cnt), recv_cnt(0), total_bytes(0), last_update_ms(get_time_ms()) {
            // 优化：预分配但不初始化，按需增长
            const std::size_t estimated_bytes = static_cast<std::size_t>(hdr.frag_cnt) * UDP_SAFE_PAYLOAD;
            data.reserve(estimated_bytes);  // 只保留容量，不初始化
            frag_recv.assign(hdr.frag_cnt, 0);
            frag_sizes.assign(hdr.frag_cnt, 0);
        }

        bool addFragment(uint16_t idx, uint16_t total, const uint8_t* payload, uint16_t size) {
            if (complete || total == 0 || idx >= total || size == 0 || size > UDP_SAFE_PAYLOAD) {
                return false;
            }
            if (frag_cnt == 0 || total != frag_cnt) {
                return false;
            }
            if (frag_recv[idx]) {
                return complete;
            }

            const std::size_t offset = static_cast<std::size_t>(idx) * UDP_SAFE_PAYLOAD;
            
            // 动态增长策略：按需扩容，带 20% 余量
            if (offset + size > data.size()) {
                std::size_t new_size = offset + size;
                std::size_t new_capacity = static_cast<std::size_t>(new_size * 1.2);
                if (new_capacity > MAX_VIDEO_FRAME_BYTES) {
                    new_capacity = MAX_VIDEO_FRAME_BYTES;
                }
                if (new_capacity > data.capacity()) {
                    data.reserve(new_capacity);
                }
                data.resize(new_size);
            }

            std::memcpy(data.data() + offset, payload, size);
            frag_recv[idx] = 1;
            frag_sizes[idx] = size;
            ++recv_cnt;
            total_bytes += size;
            last_update_ms = get_time_ms();

            if (recv_cnt == frag_cnt) {
                // 收缩到实际大小
                data.resize(total_bytes);
                complete = true;
            }

            return complete;
        }
    };

    static uint64_t get_time_ms() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    static uint64_t get_time_us() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    std::shared_ptr<EpollManager> epoll_manager_;
    int fd_;
    int listen_port_;
    FrameCallback frame_callback_;
    std::thread thread_;
    
    // 成员变量管理 assemblers，避免每次重新创建
    std::unordered_map<uint32_t, FrameAssembler> assemblers_;
    static constexpr std::size_t MAX_FRAMES_ = 60;  // 最大并发帧数
    static constexpr uint64_t timeout_ms_ = 2000;   // 超时 2 秒

    struct Stats {
        std::atomic<uint64_t> pkt_cnt{0};
        std::atomic<uint64_t> frame_cnt{0};
        std::atomic<uint64_t> dropped_pkt_cnt{0};
        std::atomic<uint64_t> dropped_frame_cnt{0};
        std::atomic<uint64_t> total_bytes{0};
        std::atomic<uint64_t> result_send_cnt{0};      // 推理结果发送次数
        std::atomic<uint64_t> result_send_bytes{0};    // 推理结果发送字节数
    } stats_;
    
    // 延迟统计
    std::atomic<uint64_t> min_latency_us_{UINT64_MAX};
    std::atomic<uint64_t> max_latency_us_{0};
    std::atomic<uint64_t> total_latency_us_{0};
    std::atomic<uint64_t> avg_latency_count_{0};
};

// ============ 主函数（示例：演示如何使用 VideoStreamInterface）============
int main(int argc, char* argv[]) {
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") {
            g_debug_mode = true;
        }
    }

    // 注册信号处理
    std::signal(SIGINT, [](int) { g_running.store(false); });
    std::signal(SIGTERM, [](int) { g_running.store(false); });

    LOG_INFO("starting video stream interface for NPU inference");

    // 创建视频流接口实例
    VideoStreamInterface video_interface(PYNQ_PORT);
    
    // 创建优化的 UDP 发送器，用于发送推理结果到 PC
    UdpSender result_sender(PC_IP, PC_CTRL_PORT);

    // 设置帧回调函数（在这里调用您的 NPU 推理代码）
    video_interface.setFrameCallback([&result_sender](const CompleteVideoFrame& frame) {
        LOG_DEBUG("[NPU] received frame=" << frame.frame_id
                  << " size=" << frame.data.size()
                  << " format=" << (int)frame.pixel_format
                  << " resolution=" << frame.width << "x" << frame.height);

        // TODO: 在这里集成您的 NPU 推理代码
        // 示例：
        // 1. 根据 frame.pixel_format 解码视频数据
        //    - PIX_FMT_MJPEG: 使用 libjpeg-turbo 解码
        //    - PIX_FMT_RGB24: 直接使用
        //    - PIX_FMT_NV12: 使用 ACL DVPP 转换
        // 2. 预处理（resize, normalize）
        // 3. 调用 Ascend ACL 进行推理
        // 4. 后处理获取结果

        // 示例：发送推理结果到 PC（使用优化的 UdpSender）
        // std::string json_result = build_result_json(...);
        // result_sender.send(json_result);
        
        // 定期打印发送统计
        static uint64_t last_print_cnt = 0;
        auto stats = result_sender.get_stats();
        if (stats.send_count > last_print_cnt + 100) {
            LOG_INFO("[UDP Sender] sent=" << stats.send_count 
                      << " bytes=" << stats.byte_count
                      << " errors=" << stats.error_count
                      << " reconnects=" << stats.reconnect_count);
            last_print_cnt = stats.send_count;
        }
    });

    LOG_INFO("video stream interface initialized, waiting for frames...");
    LOG_INFO("press Ctrl+C to stop");

    // 启动接收（阻塞模式，使用 epoll）
    video_interface.start();

    LOG_INFO("shutting down...");
    video_interface.printStats();
    
    // 打印 UDP 发送统计
    auto sender_stats = result_sender.get_stats();
    LOG_INFO("[UDP Sender] total sent=" << sender_stats.send_count 
              << " bytes=" << sender_stats.byte_count
              << " errors=" << sender_stats.error_count
              << " reconnects=" << sender_stats.reconnect_count);

    return 0;
}