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
        : listen_port_(listen_port), frame_callback_(callback) {
        fd_ = create_socket();
        if (fd_ >= 0) {
            tune_socket_buffers(fd_, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES);
            if (!bind_ip_port(fd_, ASCEND_310B_IP, listen_port_)) {
                close(fd_);
                fd_ = -1;
            }
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

    // 启动接收线程（阻塞）
    void start() {
        if (fd_ < 0) {
            LOG_ERROR("socket not initialized");
            return;
        }

        LOG_INFO("video stream interface started on port " << listen_port_);

        std::unordered_map<uint32_t, FrameAssembler> assemblers;
        constexpr std::size_t MAX_FRAMES = 60;  // 最大并发帧数
        const uint64_t timeout_ms = 2000;  // 超时 2 秒

        while (g_running.load()) {
            std::vector<uint8_t> buffer;
            sockaddr_in src{};
            socklen_t srclen = sizeof(src);

            int n = recv_bytes(fd_, &buffer, &src, &srclen);
            if (n <= 0) {
                continue;
            }

            // 解析包头
            VideoPacketHeader hdr{};
            if (!parse_video_header(buffer, &hdr)) {
                LOG_DEBUG("invalid video packet header");
                stats_.dropped_pkt_cnt.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            // 获取或创建组装器
            auto it = assemblers.find(hdr.frame_id);
            if (it == assemblers.end()) {
                // 清理超时的旧帧
                uint64_t now_ms = get_time_ms();
                for (auto fit = assemblers.begin(); fit != assemblers.end();) {
                    if (now_ms - fit->second.last_update_ms > timeout_ms) {
                        stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
                        fit = assemblers.erase(fit);
                    } else {
                        ++fit;
                    }
                }

                // 如果太多帧，移除最旧的
                if (assemblers.size() >= MAX_FRAMES) {
                    auto oldest = assemblers.begin();
                    for (auto it_old = assemblers.begin(); it_old != assemblers.end(); ++it_old) {
                        if (it_old->second.last_update_ms < oldest->second.last_update_ms) {
                            oldest = it_old;
                        }
                    }
                    stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
                    assemblers.erase(oldest);
                }

                assemblers[hdr.frame_id] = FrameAssembler(hdr);
                it = assemblers.find(hdr.frame_id);
            }

            FrameAssembler& assembler = it->second;

            if (hdr.frag_cnt != assembler.frag_cnt) {
                stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
                assemblers.erase(it);
                continue;
            }
            if (hdr.frag_idx + 1 < hdr.frag_cnt && hdr.payload_bytes != UDP_SAFE_PAYLOAD) {
                stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
                assemblers.erase(it);
                continue;
            }

            // 添加分片
            if (assembler.addFragment(hdr.frag_idx, hdr.frag_cnt,
                                      buffer.data() + sizeof(VideoPacketHeader),
                                      hdr.payload_bytes)) {
                // 帧组装完成
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
                assemblers.erase(it);
            }

            stats_.pkt_cnt.fetch_add(1, std::memory_order_relaxed);

            // 定期打印统计
            if (stats_.pkt_cnt.load() % 500 == 0) {
                printStats();
            }
        }
    }

    // 后台启动
    void start_background() {
        thread_ = std::thread(&VideoStreamInterface::start, this);
    }

    // 停止
    void stop() {
        g_running.store(false);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // 打印统计信息
    void printStats() const {
        std::cout << "[310B] [STATS] frames=" << stats_.frame_cnt.load()
                  << " pkts=" << stats_.pkt_cnt.load()
                  << " drop_pkt=" << stats_.dropped_pkt_cnt.load()
                  << " drop_frame=" << stats_.dropped_frame_cnt.load()
                  << " bytes=" << stats_.total_bytes.load() << std::endl;
    }

private:
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
            const std::size_t max_bytes = static_cast<std::size_t>(hdr.frag_cnt) * UDP_SAFE_PAYLOAD;
            data.assign(max_bytes, 0);
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
            if (offset + size > data.size()) {
                return false;
            }

            std::memcpy(data.data() + offset, payload, size);
            frag_recv[idx] = 1;
            frag_sizes[idx] = size;
            ++recv_cnt;
            total_bytes += size;
            last_update_ms = get_time_ms();

            if (recv_cnt == frag_cnt) {
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

    int fd_ = -1;
    int listen_port_;
    FrameCallback frame_callback_;
    std::thread thread_;

    struct Stats {
        std::atomic<uint64_t> pkt_cnt{0};
        std::atomic<uint64_t> frame_cnt{0};
        std::atomic<uint64_t> dropped_pkt_cnt{0};
        std::atomic<uint64_t> dropped_frame_cnt{0};
        std::atomic<uint64_t> total_bytes{0};
    } stats_;
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

    // 设置帧回调函数（在这里调用您的 NPU 推理代码）
    video_interface.setFrameCallback([](const CompleteVideoFrame& frame) {
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

        // 注意：如果需要将结果发送给 PC，可以使用 udp_net::send_bytes
        // sockaddr_in pc_addr = make_addr(PC_IP, PC_CTRL_PORT);
        // std::string json_result = build_result_json(...);
        // send_bytes(fd, json_result.data(), json_result.size(), pc_addr);
    });

    LOG_INFO("video stream interface initialized, waiting for frames...");
    LOG_INFO("press Ctrl+C to stop");

    // 启动接收（阻塞模式）
    video_interface.start();

    LOG_INFO("shutting down...");
    video_interface.printStats();

    return 0;
}