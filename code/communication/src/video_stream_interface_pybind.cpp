#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include "udp_common.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <limits>
#include <cstring>
#include <unordered_map>
#include <functional>

using namespace udp_net;
namespace py = pybind11;

// ============ 日志宏定义 ============
#define LOG_INFO(msg) do { \
    std::cout << "[310B-PyBind] [INFO] " << msg << std::endl; \
} while(0)

#define LOG_ERROR(msg) do { \
    std::cerr << "[310B-PyBind] [ERROR] " << msg << std::endl; \
} while(0)

#define LOG_DEBUG(msg) do { \
    std::cout << "[310B-PyBind] [DEBUG] " << msg << std::endl; \
} while(0)

// ============ 完整视频帧结构（Python 可访问）============
struct PyCompleteVideoFrame {
    uint32_t frame_id = 0;                           // 帧序号
    uint64_t timestamp_us = 0;                       // 时间戳（微秒）
    uint16_t width = 0;                              // 视频宽度
    uint16_t height = 0;                             // 视频高度
    uint8_t pixel_format = 0;                        // 像素格式
    std::vector<uint8_t> data;                       // 完整帧数据

    // 转换为字典，方便 Python 使用
    py::dict to_dict() const {
        py::dict d;
        d["frame_id"] = frame_id;
        d["timestamp_us"] = timestamp_us;
        d["width"] = width;
        d["height"] = height;
        d["pixel_format"] = pixel_format;
        d["data"] = data;
        return d;
    }

    // 从字典创建
    static PyCompleteVideoFrame from_dict(const py::dict& d) {
        PyCompleteVideoFrame frame;
        frame.frame_id = d["frame_id"].cast<uint32_t>();
        frame.timestamp_us = d["timestamp_us"].cast<uint64_t>();
        frame.width = d["width"].cast<uint16_t>();
        frame.height = d["height"].cast<uint16_t>();
        frame.pixel_format = d["pixel_format"].cast<uint8_t>();
        frame.data = d["data"].cast<std::vector<uint8_t>>();
        return frame;
    }
};

// ============ UDP 发送器类（用于将推理结果发送到 PC）============
class UdpSender {
public:
    UdpSender() {
        fd_ = create_socket();
        if (fd_ >= 0) {
            tune_socket_buffers(fd_, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES);
        }
    }

    ~UdpSender() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    // 发送推理结果到 PC
    int send_inference_result(const std::string& json_result,
                               const std::string& dst_ip = PC_IP,
                               int dst_port = PC_CTRL_PORT) {
        if (fd_ < 0) {
            LOG_ERROR("socket not initialized");
            return -1;
        }
        sockaddr_in addr = make_addr(dst_ip, dst_port);
        return send_bytes(fd_, reinterpret_cast<const uint8_t*>(json_result.c_str()),
                         json_result.size(), addr);
    }

    // 发送二进制数据到 PC
    int send_binary_data(const py::array_t<uint8_t>& data,
                         const std::string& dst_ip = PC_IP,
                         int dst_port = PC_CTRL_PORT) {
        if (fd_ < 0) {
            LOG_ERROR("socket not initialized");
            return -1;
        }
        auto buf = data.request();
        sockaddr_in addr = make_addr(dst_ip, dst_port);
        return send_bytes(fd_, static_cast<const uint8_t*>(buf.ptr), buf.size, addr);
    }

    // 发送文本消息到 PYNQ
    int send_control_to_pynq(const std::string& msg,
                             const std::string& dst_ip = PYNQ_IP,
                             int dst_port = PYNQ_PORT) {
        if (fd_ < 0) {
            LOG_ERROR("socket not initialized");
            return -1;
        }
        sockaddr_in addr = make_addr(dst_ip, dst_port);
        return send_msg(fd_, msg, addr);
    }

private:
    int fd_ = -1;
};

// ============ 视频流接口类（供 Python 调用）============
class PyVideoStreamInterface {
public:
    using PyFrameCallback = py::object;

    PyVideoStreamInterface(int listen_port = PYNQ_PORT)
        : listen_port_(listen_port) {
        fd_ = create_socket();
        if (fd_ >= 0) {
            tune_socket_buffers(fd_, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES);
            if (!bind_ip_port(fd_, ASCEND_310B_IP, listen_port_)) {
                close(fd_);
                fd_ = -1;
                throw std::runtime_error("Failed to bind port " + std::to_string(listen_port_));
            }
        } else {
            throw std::runtime_error("Failed to create socket");
        }

        // 创建 epoll 管理器并添加 socket
        epoll_mgr_ = std::make_unique<EpollManager>(64);
        auto callback = [this](int fd) { this->handleSocketEvent(fd); };
        if (!epoll_mgr_->add_socket(fd_, callback)) {
            throw std::runtime_error("Failed to add socket to epoll");
        }

        LOG_INFO("VideoStreamInterface created on port " << listen_port_);
    }

    ~PyVideoStreamInterface() {
        // 先停止 epoll 线程，确保不再调用 Python 回调
        stop();

        // 在持有 GIL 的情况下清空回调函数，避免访问已销毁的 Python 对象
        {
            py::gil_scoped_acquire acquire_gil;
            frame_callback_ = py::object();
        }

        if (fd_ >= 0) {
            close(fd_);
        }
    }

    // 处理 socket 事件（由 epoll 回调）
    void handleSocketEvent(int fd) {
        std::vector<uint8_t> buffer;
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);

        int n = recv_bytes(fd, &buffer, &src, &srclen);
        if (n <= 0) {
            return;
        }
        stats_.recv_pkt_cnt.fetch_add(1, std::memory_order_relaxed);

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
                    stats_.dropped_frag_cnt.fetch_add(
                        static_cast<uint64_t>(fit->second.frag_cnt - fit->second.recv_cnt),
                        std::memory_order_relaxed);
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
                stats_.dropped_frag_cnt.fetch_add(
                    static_cast<uint64_t>(oldest->second.frag_cnt - oldest->second.recv_cnt),
                    std::memory_order_relaxed);
                stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
                assemblers_.erase(oldest);
            }

            assemblers_[hdr.frame_id] = FrameAssembler(hdr);
            it = assemblers_.find(hdr.frame_id);
        }

        FrameAssembler& assembler = it->second;

        if (hdr.frag_cnt != assembler.frag_cnt) {
            stats_.dropped_frag_cnt.fetch_add(
                static_cast<uint64_t>(assembler.frag_cnt - assembler.recv_cnt),
                std::memory_order_relaxed);
            stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
            assemblers_.erase(it);
            return;
        }
        if (hdr.frag_idx + 1 < hdr.frag_cnt && hdr.payload_bytes != UDP_SAFE_PAYLOAD) {
            stats_.dropped_frag_cnt.fetch_add(
                static_cast<uint64_t>(assembler.frag_cnt - assembler.recv_cnt),
                std::memory_order_relaxed);
            stats_.dropped_frame_cnt.fetch_add(1, std::memory_order_relaxed);
            assemblers_.erase(it);
            return;
        }

        // 添加分片
        if (assembler.addFragment(hdr.frag_idx, hdr.frag_cnt,
                                  buffer.data() + sizeof(VideoPacketHeader),
                                  hdr.payload_bytes)) {
            // 帧组装完成
            PyCompleteVideoFrame frame;
            frame.frame_id = hdr.frame_id;
            frame.timestamp_us = hdr.timestamp_us;
            frame.width = hdr.width;
            frame.height = hdr.height;
            frame.pixel_format = hdr.pixel_format;

            // 复制到纯 C++ 缓冲区，避免在后台线程中创建/销毁 NumPy 对象
            frame.data = assembler.data;

            stats_.frame_cnt.fetch_add(1, std::memory_order_relaxed);
            stats_.total_bytes.fetch_add(frame.data.size(), std::memory_order_relaxed);

            const uint64_t now_us = get_time_us();
            const uint64_t assembly_latency_us = now_us >= assembler.first_update_us
                ? (now_us - assembler.first_update_us)
                : 0;
            const uint64_t sender_latency_us = now_us >= hdr.timestamp_us
                ? (now_us - hdr.timestamp_us)
                : 0;
            stats_.record_latency(assembly_latency_us, sender_latency_us);

            // 调用 Python 回调
            if (frame_callback_ && !frame_callback_.is_none()) {
                // ✅ 使用 pybind11 RAII 方式获取 GIL
                py::gil_scoped_acquire acquire_gil;

                try {
                    // 调用 Python 回调（frame 会自动通过 pybind11 转换为 py::dict）
                    py::object result = frame_callback_(frame);

                    // 如果回调返回了推理结果，发送到 PC
                    if (py::isinstance<py::dict>(result) &&
                        result.attr("contains")("inference_result").cast<bool>()) {
                        std::string json_result = result["inference_result"].cast<std::string>();
                        if (!json_result.empty() && udp_sender_) {
                            udp_sender_->send_inference_result(json_result);
                            LOG_DEBUG("Sent inference result for frame " << frame.frame_id);
                        }
                    }
                } catch (const py::error_already_set& e) {
                    LOG_ERROR("Python callback error: " << e.what());
                } catch (const std::exception& e) {
                    LOG_ERROR("Callback error: " << e.what());
                }
            }

            // 移除已完成的帧
            assemblers_.erase(it);
        stats_.pkt_cnt.fetch_add(1, std::memory_order_relaxed);

        // 定期打印统计
        if (stats_.pkt_cnt.load() % 500 == 0) {
            printStats();
        }
    }
        }

    // 设置帧回调函数（Python 函数）
    void set_frame_callback(PyFrameCallback callback) {
        py::gil_scoped_acquire acquire_gil;
        frame_callback_ = std::move(callback);
    }

    // 启动接收线程（阻塞）- 使用 epoll
    void start() {
        if (fd_ < 0 || !epoll_mgr_) {
            LOG_ERROR("socket not initialized");
            return;
        }

        LOG_INFO("video stream interface started on port " << listen_port_);
        epoll_mgr_->start();
    }

    // 后台启动
    void start_background() {
        if (!epoll_mgr_) {
            LOG_ERROR("epoll manager not initialized");
            return;
        }
        epoll_mgr_->start_background();
    }

    // 停止
    void stop() {
        if (epoll_mgr_) {
            epoll_mgr_->stop();
        }
        g_running.store(false);
        // 注意：epoll 线程已 detach，不需要 join
    }

    // 打印统计信息
    void printStats() const {
        auto d = get_detailed_stats();
        const double loss_rate = d["packet_loss_rate_per_mille"] / 10.0;
        std::cout << "[310B-PyBind] [STATS] "
                  << "recv_pkt=" << d["recv_pkt_cnt"]
                  << " valid_pkt=" << d["pkt_cnt"]
                  << " drop_pkt=" << d["dropped_pkt_cnt"]
                  << " drop_frag=" << d["dropped_frag_cnt"]
                  << " drop_frame=" << d["dropped_frame_cnt"]
                  << " frames=" << d["frame_cnt"]
                  << " bytes=" << d["total_bytes"]
                  << " loss=" << loss_rate << "%"
                  << " latency_asm(us,min/max/avg)="
                  << d["assembly_latency_min_us"] << "/"
                  << d["assembly_latency_max_us"] << "/"
                  << d["assembly_latency_avg_us"]
                  << " latency_e2e(us,min/max/avg)="
                  << d["sender_latency_min_us"] << "/"
                  << d["sender_latency_max_us"] << "/"
                  << d["sender_latency_avg_us"]
                  << std::endl;
    }

    std::unordered_map<std::string, uint64_t> get_detailed_stats() const {
        std::unordered_map<std::string, uint64_t> stats;
        const uint64_t recv_pkt_cnt = stats_.recv_pkt_cnt.load();
        const uint64_t valid_pkt_cnt = stats_.pkt_cnt.load();
        const uint64_t dropped_pkt_cnt = stats_.dropped_pkt_cnt.load();
        const uint64_t dropped_frame_cnt = stats_.dropped_frame_cnt.load();
        const uint64_t dropped_frag_cnt = stats_.dropped_frag_cnt.load();
        const uint64_t frame_cnt = stats_.frame_cnt.load();
        const uint64_t total_bytes = stats_.total_bytes.load();
        const uint64_t packet_loss_den = recv_pkt_cnt + dropped_frag_cnt;
        const uint64_t packet_loss_num = dropped_pkt_cnt + dropped_frag_cnt;
        const uint64_t frame_loss_den = frame_cnt + dropped_frame_cnt;

        stats["recv_pkt_cnt"] = recv_pkt_cnt;
        stats["pkt_cnt"] = valid_pkt_cnt;
        stats["dropped_pkt_cnt"] = dropped_pkt_cnt;
        stats["dropped_frame_cnt"] = dropped_frame_cnt;
        stats["dropped_frag_cnt"] = dropped_frag_cnt;
        stats["frame_cnt"] = frame_cnt;
        stats["total_bytes"] = total_bytes;
        stats["packet_loss_rate_per_mille"] = packet_loss_den > 0 ? (packet_loss_num * 1000 / packet_loss_den) : 0;
        stats["frame_loss_rate_per_mille"] = frame_loss_den > 0 ? (dropped_frame_cnt * 1000 / frame_loss_den) : 0;
        stats["assembly_latency_min_us"] = stats_.assembly_latency_min_us.load();
        stats["assembly_latency_max_us"] = stats_.assembly_latency_max_us.load();
        stats["assembly_latency_avg_us"] = stats_.latency_cnt.load() > 0
            ? stats_.total_assembly_latency_us.load() / stats_.latency_cnt.load()
            : 0;
        stats["sender_latency_min_us"] = stats_.sender_latency_min_us.load();
        stats["sender_latency_max_us"] = stats_.sender_latency_max_us.load();
        stats["sender_latency_avg_us"] = stats_.latency_cnt.load() > 0
            ? stats_.total_sender_latency_us.load() / stats_.latency_cnt.load()
            : 0;
        return stats;
    }

    // 获取 UDP 发送器
    UdpSender* get_udp_sender() {
        if (!udp_sender_) {
            udp_sender_ = std::make_unique<UdpSender>();
        }
        return udp_sender_.get();
    }

private:
    // 类成员变量移到 private
    static constexpr std::size_t MAX_FRAMES_ = 60;  // 最大并发帧数
    static constexpr uint64_t timeout_ms_ = 2000;   // 超时 2 秒

    struct FrameAssembler {
        std::vector<uint8_t> data;
        std::vector<uint8_t> frag_recv;
        std::vector<uint16_t> frag_sizes;
        uint16_t frag_cnt = 0;
        uint16_t recv_cnt = 0;
        std::size_t total_bytes = 0;
        uint64_t last_update_ms = 0;
        uint64_t first_update_us = 0;
        bool complete = false;

        FrameAssembler() : last_update_ms(get_time_ms()), first_update_us(get_time_us()) {}
        explicit FrameAssembler(const VideoPacketHeader& hdr)
            : frag_cnt(hdr.frag_cnt), recv_cnt(0), total_bytes(0), last_update_ms(get_time_ms()), first_update_us(get_time_us()) {
            const std::size_t max_bytes = static_cast<std::size_t>(hdr.frag_cnt) * UDP_SAFE_PAYLOAD;
            data.reserve(max_bytes);  // 预分配，但不初始化
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

            // 动态增长缓冲区
            const std::size_t required_size = static_cast<std::size_t>(idx) * UDP_SAFE_PAYLOAD + size;
            if (data.size() < required_size) {
                std::size_t new_size = data.empty() ? required_size : data.size();
                while (new_size < required_size) {
                    new_size = new_size * 3 / 2 + UDP_SAFE_PAYLOAD;  // 增长 50%
                }
                if (new_size > MAX_VIDEO_FRAME_BYTES) {
                    new_size = MAX_VIDEO_FRAME_BYTES;
                }
                data.resize(new_size);
            }

            std::memcpy(data.data() + static_cast<std::size_t>(idx) * UDP_SAFE_PAYLOAD, payload, size);
            frag_recv[idx] = 1;
            frag_sizes[idx] = size;
            ++recv_cnt;
            total_bytes += size;
            last_update_ms = get_time_ms();

            if (recv_cnt == frag_cnt) {
                data.resize(total_bytes);  // 收缩到实际大小
                complete = true;
            }

            return complete;
        }
    };

    std::unordered_map<uint32_t, FrameAssembler> assemblers_;  // 成员变量

public:
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

    int fd_ = -1;
    int listen_port_;
    PyFrameCallback frame_callback_;
    std::unique_ptr<UdpSender> udp_sender_;
    std::unique_ptr<EpollManager> epoll_mgr_;  // epoll 管理器

    struct Stats {
        std::atomic<uint64_t> recv_pkt_cnt{0};
        std::atomic<uint64_t> pkt_cnt{0};
        std::atomic<uint64_t> frame_cnt{0};
        std::atomic<uint64_t> dropped_pkt_cnt{0};
        std::atomic<uint64_t> dropped_frame_cnt{0};
        std::atomic<uint64_t> dropped_frag_cnt{0};
        std::atomic<uint64_t> total_bytes{0};
        std::atomic<uint64_t> latency_cnt{0};
        std::atomic<uint64_t> total_assembly_latency_us{0};
        std::atomic<uint64_t> total_sender_latency_us{0};
        std::atomic<uint64_t> assembly_latency_min_us{std::numeric_limits<uint64_t>::max()};
        std::atomic<uint64_t> assembly_latency_max_us{0};
        std::atomic<uint64_t> sender_latency_min_us{std::numeric_limits<uint64_t>::max()};
        std::atomic<uint64_t> sender_latency_max_us{0};

        void record_latency(uint64_t assembly_latency_us, uint64_t sender_latency_us) {
            latency_cnt.fetch_add(1, std::memory_order_relaxed);
            total_assembly_latency_us.fetch_add(assembly_latency_us, std::memory_order_relaxed);
            total_sender_latency_us.fetch_add(sender_latency_us, std::memory_order_relaxed);
            update_min(assembly_latency_min_us, assembly_latency_us);
            update_max(assembly_latency_max_us, assembly_latency_us);
            update_min(sender_latency_min_us, sender_latency_us);
            update_max(sender_latency_max_us, sender_latency_us);
        }

    private:
        static void update_min(std::atomic<uint64_t>& target, uint64_t value) {
            uint64_t cur = target.load(std::memory_order_relaxed);
            while (value < cur && !target.compare_exchange_weak(
                       cur, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
            }
        }

        static void update_max(std::atomic<uint64_t>& target, uint64_t value) {
            uint64_t cur = target.load(std::memory_order_relaxed);
            while (value > cur && !target.compare_exchange_weak(
                       cur, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
            }
        }
    } stats_;

    std::atomic<bool> g_running{true};
};

// ============ Pybind11 模块定义 ============
PYBIND11_MODULE(video_stream_interface, m) {
    m.doc() = "Video Stream Interface for Ascend 310B - Python Binding";

    // 导出常量
    m.attr("PC_IP") = PC_IP;
    m.attr("PYNQ_IP") = PYNQ_IP;
    m.attr("ASCEND_310B_IP") = ASCEND_310B_IP;
    m.attr("ASCEND_310B_PC_IP") = ASCEND_310B_PC_IP;
    m.attr("PC_PORT") = py::int_(PC_PORT);
    m.attr("PYNQ_PORT") = py::int_(PYNQ_PORT);
    m.attr("PC_CTRL_PORT") = py::int_(PC_CTRL_PORT);
    m.attr("VIDEO_WIDTH") = py::int_(VIDEO_WIDTH);
    m.attr("VIDEO_HEIGHT") = py::int_(VIDEO_HEIGHT);

    // 像素格式常量
    m.attr("PIX_FMT_RGB24") = py::int_(PIX_FMT_RGB24);
    m.attr("PIX_FMT_MJPEG") = py::int_(PIX_FMT_MJPEG);
    m.attr("PIX_FMT_NV12") = py::int_(PIX_FMT_NV12);

    // 导出 PyCompleteVideoFrame 类
    py::class_<PyCompleteVideoFrame>(m, "CompleteVideoFrame")
        .def(py::init<>(), "Create an empty video frame")
        .def_readwrite("frame_id", &PyCompleteVideoFrame::frame_id)
        .def_readwrite("timestamp_us", &PyCompleteVideoFrame::timestamp_us)
        .def_readwrite("width", &PyCompleteVideoFrame::width)
        .def_readwrite("height", &PyCompleteVideoFrame::height)
        .def_readwrite("pixel_format", &PyCompleteVideoFrame::pixel_format)
        .def_readwrite("data", &PyCompleteVideoFrame::data)
        .def("to_dict", &PyCompleteVideoFrame::to_dict, "Convert to Python dictionary")
        .def_static("from_dict", &PyCompleteVideoFrame::from_dict, "Create from Python dictionary",
                    py::arg("d"))
        .def("__repr__", [](const PyCompleteVideoFrame& f) {
            return "<CompleteVideoFrame frame_id=" + std::to_string(f.frame_id) +
                   " size=" + std::to_string(f.width) + "x" + std::to_string(f.height) +
                   " format=" + std::to_string(f.pixel_format) + ">";
        });

    // 导出 UdpSender 类
    py::class_<UdpSender>(m, "UdpSender")
        .def(py::init<>(), "Create UDP sender")
        .def("send_inference_result", &UdpSender::send_inference_result,
             "Send inference result (JSON) to PC",
             py::arg("json_result"),
             py::arg("dst_ip") = PC_IP,
             py::arg("dst_port") = PC_CTRL_PORT)
        .def("send_binary_data", &UdpSender::send_binary_data,
             "Send binary data to PC",
             py::arg("data"),
             py::arg("dst_ip") = PC_IP,
             py::arg("dst_port") = PC_CTRL_PORT)
        .def("send_control_to_pynq", &UdpSender::send_control_to_pynq,
             "Send control message to PYNQ",
             py::arg("msg"),
             py::arg("dst_ip") = PYNQ_IP,
             py::arg("dst_port") = PYNQ_PORT);

    // 导出 VideoStreamInterface 类
    py::class_<PyVideoStreamInterface>(m, "VideoStreamInterface")
        .def(py::init<int>(), "Create video stream interface",
             py::arg("listen_port") = PYNQ_PORT)
        .def("set_frame_callback", &PyVideoStreamInterface::set_frame_callback,
             "Set frame callback function (Python function)")
        .def("start", &PyVideoStreamInterface::start,
             "Start receiving (blocking mode)")
        .def("start_background", &PyVideoStreamInterface::start_background,
             "Start receiving in background thread")
        .def("stop", &PyVideoStreamInterface::stop,
             "Stop receiving")
        .def("print_stats", &PyVideoStreamInterface::printStats,
             "Print statistics")
           .def("get_detailed_stats", &PyVideoStreamInterface::get_detailed_stats,
               "Get detailed statistics as a dict")
        .def("get_udp_sender", &PyVideoStreamInterface::get_udp_sender,
             "Get UDP sender instance",
             py::return_value_policy::reference_internal);

    // 辅助函数
    m.def("get_time_ms", &PyVideoStreamInterface::get_time_ms,
          "Get current time in milliseconds");
}