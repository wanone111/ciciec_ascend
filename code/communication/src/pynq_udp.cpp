#include "udp_common.h"                             // 引入 UDP 公共工具头文件
#include <chrono>                                    // 引入节拍控制
#include <csignal>                                   // 引入信号处理
#include <cstdlib>                                   // 引入 getenv
#include <filesystem>                                // 引入日志目录创建
#include <fcntl.h>                                   // 引入 open 文件标志
#include <fstream>                                   // 引入 JSONL 日志文件
#include <iostream>                                  // 引入标准输出流
#include <linux/videodev2.h>                         // 引入 V4L2 摄像头接口
#include <sstream>                                   // 引入字符串流
#include <string>                                    // 引入字符串类型
#include <sys/ioctl.h>                               // 引入 ioctl
#include <sys/mman.h>                                // 引入 mmap/munmap
#include <thread>                                    // 引入线程休眠
#include <vector>                                    // 引入动态数组

using namespace udp_net;                             // 使用 udp_net 命名空间

namespace {                                          // 匿名命名空间，限制内部符号可见范围
volatile std::sig_atomic_t g_stop = 0;               // 停止标记，由信号处理函数置位

constexpr const char* PYNQ_DEFAULT_LOG_DIR = "/home/xilinx/jupyter_notebooks/ciciec/log";

void handle_signal(int) {                            // 信号处理函数
    g_stop = 1;                                      // 收到停止信号后请求退出主循环
}

uint64_t unix_time_us() {                             // 返回 Unix 微秒时间戳，便于三端对齐
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string json_escape(const std::string& value) {   // 简单 JSON 字符串转义
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch; break;
        }
    }
    return out.str();
}

std::string env_or_default_log_dir() {                // 支持现场通过环境变量覆盖 PYNQ 日志目录
    if (const char* v = std::getenv("SMART_IP_PYNQ_BENCH_DIR")) {
        return v;
    }
    if (const char* v = std::getenv("PYNQ_BENCH_DIR")) {
        return v;
    }
    if (const char* v = std::getenv("SMART_IP_BENCH_DIR")) {
        return v;
    }
    return PYNQ_DEFAULT_LOG_DIR;
}

class JsonlLogger {                                   // PYNQ 端轻量 JSONL 压测日志器
public:
    explicit JsonlLogger(const std::string& component)
        : component_(component) {
        const std::string dir = env_or_default_log_dir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::cerr << "[PYNQ] create log dir failed: " << dir
                      << " err=" << ec.message() << std::endl;
            return;
        }

        path_ = (std::filesystem::path(dir) / (component + ".jsonl")).string();
        file_.open(path_, std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "[PYNQ] open log failed: " << path_ << std::endl;
        } else {
            std::cout << "[PYNQ] benchmark log: " << path_ << std::endl;
        }
    }

    bool enabled() const {
        return file_.is_open();
    }

    void log_raw(const std::string& kind, const std::string& payload = "") {
        if (!file_.is_open()) {
            return;
        }

        const uint64_t now_us = unix_time_us();
        file_ << "{\"component\":\"" << json_escape(component_) << "\""
              << ",\"kind\":\"" << json_escape(kind) << "\""
              << ",\"ts\":" << (static_cast<double>(now_us) / 1000000.0)
              << ",\"ts_us\":" << now_us;
        if (!payload.empty()) {
            file_ << "," << payload;
        }
        file_ << "}\n";
        file_.flush();
    }

private:
    std::string component_;
    std::string path_;
    std::ofstream file_;
};

class V4L2Rgb24Reader {                              // 简化版 V4L2 读帧器（read() 模式）
public:
    bool open_device(const std::string& dev, uint16_t width, uint16_t height) { // 打开并配置摄像头
        fd_ = open(dev.c_str(), O_RDWR);             // 以读写方式打开摄像头设备
        if (fd_ < 0) {                               // 打开失败分支
            std::cerr << "[PYNQ] open camera failed: " << dev << " err=" << strerror(errno) << std::endl; // 打印错误
            return false;                            // 返回失败
        }

        v4l2_capability cap{};                       // 定义能力查询结构体
        if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) { // 查询设备能力
            std::cerr << "[PYNQ] VIDIOC_QUERYCAP failed: " << strerror(errno) << std::endl; // 打印错误
            close_device();                          // 关闭设备
            return false;                            // 返回失败
        }

        if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) { // 检查是否支持视频采集
            std::cerr << "[PYNQ] device has no VIDEO_CAPTURE capability" << std::endl; // 打印错误
            close_device();                          // 关闭设备
            return false;                            // 返回失败
        }

        if ((cap.capabilities & V4L2_CAP_STREAMING) == 0) { // 检查是否支持 STREAMING（mmap 模式）
            std::cerr << "[PYNQ] device has no STREAMING capability" << std::endl; // 打印错误
            close_device();                          // 关闭设备
            return false;                            // 返回失败
        }

        v4l2_format fmt{};                           // 定义格式配置结构体
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;      // 设置为视频采集类型
        fmt.fmt.pix.width = width;                   // 设置目标宽度
        fmt.fmt.pix.height = height;                 // 设置目标高度
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24; // 设置像素格式为 RGB24
        fmt.fmt.pix.field = V4L2_FIELD_NONE;         // 设置逐行扫描
        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {    // 下发格式配置
            std::cerr << "[PYNQ] VIDIOC_S_FMT RGB24 failed: " << strerror(errno) << std::endl; // 打印错误
            close_device();                          // 关闭设备
            return false;                            // 返回失败
        }

        width_ = static_cast<uint16_t>(fmt.fmt.pix.width); // 记录驱动最终生效宽度
        height_ = static_cast<uint16_t>(fmt.fmt.pix.height); // 记录驱动最终生效高度
        sizeimage_ = static_cast<std::size_t>(fmt.fmt.pix.sizeimage); // 记录驱动建议的单帧最大字节数

        if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24) { // 驱动实际返回 RGB24 分支
            pixel_format_ = PIX_FMT_RGB24;             // 记录协议像素格式为 RGB24
            std::cout << "[PYNQ] camera output RGB24" << std::endl; // 打印像素格式日志
        } else if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) { // 驱动实际返回 MJPEG 分支
            pixel_format_ = PIX_FMT_MJPEG;            // 记录协议像素格式为 MJPEG
            std::cout << "[PYNQ] camera fallback to MJPEG" << std::endl; // 打印像素格式降级日志
        } else {                                      // 其他像素格式暂不支持
            std::cerr << "[PYNQ] unsupported camera format fourcc=0x"
                      << std::hex << fmt.fmt.pix.pixelformat << std::dec << std::endl; // 打印格式错误
            close_device();                           // 关闭设备
            return false;                             // 返回失败
        }

        if (!init_mmap_buffers()) {                  // 初始化 mmap 缓冲区并启动视频流
            close_device();                          // 初始化失败时关闭设备
            return false;                            // 返回失败
        }

        std::cout << "[PYNQ] camera ready: " << width_ << "x" << height_
                  << " sizeimage=" << sizeimage_ << std::endl; // 打印摄像头参数
        return true;                                 // 返回成功
    }

    bool read_frame(std::vector<uint8_t>* frame) {   // 读取一帧 RGB24 数据
        if (fd_ < 0 || !frame || !streaming_) {      // 基础合法性检查
            return false;                            // 参数非法直接失败
        }

        v4l2_buffer buf{};                           // 定义出队缓冲区描述符
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;      // 指定采集缓冲区类型
        buf.memory = V4L2_MEMORY_MMAP;               // 指定内存类型为 mmap
        if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {   // 出队一帧可用缓冲区
            if (errno == EAGAIN || errno == EINTR) { // 非致命异常直接重试
                return false;                        // 返回失败交给上层短暂退避
            }
            std::cerr << "[PYNQ] VIDIOC_DQBUF failed: " << strerror(errno) << std::endl; // 打印错误
            return false;                            // 返回失败
        }

        if (buf.index >= buffers_.size()) {          // 缓冲区索引合法性检查
            std::cerr << "[PYNQ] invalid buffer index from camera: " << buf.index << std::endl; // 打印错误
            return false;                            // 返回失败
        }

        const std::size_t frame_bytes = static_cast<std::size_t>(buf.bytesused); // 获取该帧真实数据长度
        frame->assign(frame_bytes, 0);               // 按真实帧长分配输出缓冲区
        std::memcpy(frame->data(), buffers_[buf.index].start, frame_bytes); // 拷贝该帧真实数据

        if (pixel_format_ == PIX_FMT_RGB24) {        // 仅 RGB24 情况检查完整帧长度
            const std::size_t expected = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 3; // 计算理论 RGB24 帧长度
            if (frame_bytes < expected) {            // 检查是否短帧
                std::cout << "[PYNQ] warning: short RGB24 frame bytesused=" << frame_bytes
                          << " need=" << expected << std::endl; // 打印短帧告警
            }
        }

        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {    // 将缓冲区重新入队供驱动复用
            std::cerr << "[PYNQ] VIDIOC_QBUF failed: " << strerror(errno) << std::endl; // 打印错误
            return false;                            // 返回失败
        }

        return true;                                 // 读取成功
    }

    uint16_t width() const { return width_; }        // 返回当前帧宽度
    uint16_t height() const { return height_; }      // 返回当前帧高度
    uint8_t pixel_format() const { return pixel_format_; } // 返回当前像素格式（RGB24/MJPEG）

    ~V4L2Rgb24Reader() {                             // 析构函数
        close_device();                              // 自动关闭设备
    }

private:
    struct MmapBuffer {                              // mmap 缓冲区描述结构体
        void* start = nullptr;                       // mmap 起始地址
        std::size_t length = 0;                      // mmap 缓冲区长度
    };

    static int xioctl(int fd, unsigned long req, void* arg) { // ioctl 包装，自动处理 EINTR
        int rc = 0;                                  // 定义 ioctl 返回值
        do {
            rc = ioctl(fd, req, arg);                // 执行 ioctl
        } while (rc == -1 && errno == EINTR);        // EINTR 时自动重试
        return rc;                                   // 返回最终结果
    }

    bool init_mmap_buffers() {                       // 申请并初始化 mmap 缓冲区
        v4l2_requestbuffers req{};                   // 定义缓冲区申请结构体
        req.count = 4;                               // 申请 4 个缓冲区，平衡延迟和稳定性
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;      // 设置缓冲区类型
        req.memory = V4L2_MEMORY_MMAP;               // 设置内存模型为 mmap
        if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0) { // 向驱动申请缓冲区
            std::cerr << "[PYNQ] VIDIOC_REQBUFS failed: " << strerror(errno) << std::endl; // 打印错误
            return false;                            // 返回失败
        }

        if (req.count < 2) {                         // 缓冲区数量下限检查
            std::cerr << "[PYNQ] insufficient mmap buffers" << std::endl; // 打印错误
            return false;                            // 返回失败
        }

        buffers_.assign(req.count, {});              // 按驱动实际返回数量分配描述数组

        for (std::size_t i = 0; i < buffers_.size(); ++i) { // 逐个查询并映射缓冲区
            v4l2_buffer buf{};                       // 定义缓冲区查询结构体
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 设置缓冲区类型
            buf.memory = V4L2_MEMORY_MMAP;           // 设置内存类型
            buf.index = static_cast<uint32_t>(i);    // 指定查询索引
            if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) { // 查询缓冲区属性
                std::cerr << "[PYNQ] VIDIOC_QUERYBUF failed: " << strerror(errno) << std::endl; // 打印错误
                return false;                        // 返回失败
            }

            void* ptr = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset); // 映射内核缓冲区到用户态
            if (ptr == MAP_FAILED) {                 // 判断映射是否失败
                std::cerr << "[PYNQ] mmap failed: " << strerror(errno) << std::endl; // 打印错误
                return false;                        // 返回失败
            }

            buffers_[i].start = ptr;                 // 保存映射起始地址
            buffers_[i].length = buf.length;         // 保存映射长度

            if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) { // 初次入队，交给驱动采集填充
                std::cerr << "[PYNQ] initial VIDIOC_QBUF failed: " << strerror(errno) << std::endl; // 打印错误
                return false;                        // 返回失败
            }
        }

        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 定义视频流类型
        if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) { // 启动采集流
            std::cerr << "[PYNQ] VIDIOC_STREAMON failed: " << strerror(errno) << std::endl; // 打印错误
            return false;                            // 返回失败
        }

        streaming_ = true;                           // 标记流已启动
        return true;                                 // 初始化成功
    }

    void stop_stream_if_needed() {                   // 安全停止视频流
        if (fd_ >= 0 && streaming_) {                // 仅在流处于启动状态时执行
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 定义视频流类型
            if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) { // 停止采集流
                std::cerr << "[PYNQ] VIDIOC_STREAMOFF failed: " << strerror(errno) << std::endl; // 打印错误
            }
            streaming_ = false;                      // 清除流启动标记
        }
    }

    void unmap_all_buffers() {                       // 释放所有 mmap 缓冲区映射
        for (auto& b : buffers_) {                   // 遍历所有缓冲区
            if (b.start && b.start != MAP_FAILED) {  // 检查映射地址合法性
                munmap(b.start, b.length);           // 释放当前缓冲区映射
            }
            b.start = nullptr;                       // 清空映射地址
            b.length = 0;                            // 清空映射长度
        }
        buffers_.clear();                            // 清空缓冲区数组
    }

    void close_device() {                            // 关闭设备内部函数
        stop_stream_if_needed();                     // 先停止视频流
        unmap_all_buffers();                         // 再释放 mmap 缓冲区
        if (fd_ >= 0) {                              // 若设备已打开
            close(fd_);                              // 关闭设备文件描述符
            fd_ = -1;                                // 重置句柄
        }
    }

    int fd_ = -1;                                    // 摄像头设备句柄
    uint16_t width_ = VIDEO_WIDTH;                   // 当前生效宽度
    uint16_t height_ = VIDEO_HEIGHT;                 // 当前生效高度
    std::size_t sizeimage_ = 0;                      // 驱动建议单帧最大字节数
    uint8_t pixel_format_ = PIX_FMT_RGB24;           // 当前输出像素格式
    bool streaming_ = false;                         // 记录视频流是否已启动
    std::vector<MmapBuffer> buffers_;                // 保存所有 mmap 缓冲区信息
};
} // namespace

int main() {                                         // 主函数入口
    std::signal(SIGINT, handle_signal);              // 注册 Ctrl+C 信号
    std::signal(SIGTERM, handle_signal);             // 注册终止信号

    int fd_pynq = create_socket();                   // 创建 PYNQ 侧 socket
    if (fd_pynq < 0) {                               // 判断创建是否失败
        return 1;                                    // 返回错误码
    }                                                // 结束失败判断

    if (!bind_ip_port(fd_pynq, PYNQ_IP, PYNQ_PORT)) { // 绑定 PYNQ 本机 IP 与监听端口
        close(fd_pynq);                              // 关闭 socket
        return 1;                                    // 返回错误码
    }                                                // 结束绑定判断

    tune_socket_buffers(fd_pynq, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES); // 增大 UDP 缓冲区，降低突发丢包概率

    sockaddr_in to_310b = make_addr(ASCEND_310B_IP, PYNQ_PORT); // 构造 310B 目标地址

    timeval control_timeout{};                       // 定义控制消息接收超时
    control_timeout.tv_sec = 0;                      // 秒级超时设置为 0
    control_timeout.tv_usec = 5000;                  // 微秒级超时 5ms，避免阻塞采集发送
    setsockopt(fd_pynq, SOL_SOCKET, SO_RCVTIMEO, &control_timeout, sizeof(control_timeout)); // 设置接收超时

    V4L2Rgb24Reader camera;                          // 创建摄像头采集器
    if (!camera.open_device("/dev/video0", VIDEO_WIDTH, VIDEO_HEIGHT)) { // 打开并配置 USB 摄像头
        close(fd_pynq);                              // 关闭 socket
        return 1;                                    // 返回错误码
    }

    JsonlLogger bench_log("pynq");                   // 创建 PYNQ 端压测日志
    bench_log.log_raw("pynq_start",
        "\"width\":" + std::to_string(camera.width()) +
        ",\"height\":" + std::to_string(camera.height()) +
        ",\"pixel_format\":" + std::to_string(static_cast<int>(camera.pixel_format())) +
        ",\"video_fps\":" + std::to_string(VIDEO_FPS) +
        ",\"dst_ip\":\"" + json_escape(ASCEND_310B_IP) + "\"" +
        ",\"dst_port\":" + std::to_string(PYNQ_PORT));

    std::cout << "[PYNQ] start RGB24 video send loop" << std::endl; // 打印启动信息
    uint32_t frame_id = 0;                           // 定义帧号计数器
    uint64_t sent_pkt_cnt = 0;                       // 统计累计发送分片数
    uint64_t sent_frame_cnt = 0;                     // 统计累计发送帧数
    uint64_t send_fail_cnt = 0;                      // 统计累计发送失败分片数
    const auto frame_period = std::chrono::microseconds(1000000 / VIDEO_FPS); // 计算目标帧间隔
    auto next_send_tp = std::chrono::steady_clock::now(); // 定义下一帧发送时刻

    while (!g_stop) {                                // 持续采集发送循环
        auto now = std::chrono::steady_clock::now(); // 获取当前时间
        if (now < next_send_tp) {                    // 到达目标发送时刻前进行节流
            std::this_thread::sleep_until(next_send_tp); // 休眠到目标发送时刻
        }

        std::vector<uint8_t> frame;                  // 定义单帧 RGB24 缓冲区
        if (!camera.read_frame(&frame)) {            // 读取一帧摄像头数据
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 读取失败后短暂退避
            continue;                                // 继续下一轮读取
        }

        uint64_t timestamp_us = unix_time_us();       // 生成可跨端对齐的 Unix 微秒时间戳

        const std::size_t frag_cnt = (frame.size() + UDP_SAFE_PAYLOAD - 1) / UDP_SAFE_PAYLOAD; // 计算该帧分片总数
        if (frag_cnt == 0 || frag_cnt > 65535) {     // 分片数量边界检查
            std::cout << "[PYNQ] skip invalid frag count=" << frag_cnt << std::endl; // 打印异常日志
            bench_log.log_raw("invalid_frame", "\"frame_id\":" + std::to_string(frame_id) +
                ",\"frame_bytes\":" + std::to_string(frame.size()) +
                ",\"frag_cnt\":" + std::to_string(frag_cnt));
            continue;                                // 跳过异常帧
        }

        uint64_t frame_sent_pkt = 0;                  // 当前帧成功发送分片数
        uint64_t frame_failed_pkt = 0;                // 当前帧发送失败分片数
        for (std::size_t frag_idx = 0; frag_idx < frag_cnt; ++frag_idx) { // 按分片顺序发送当前帧
            const std::size_t offset = frag_idx * UDP_SAFE_PAYLOAD; // 计算当前分片偏移
            const std::size_t payload = std::min<std::size_t>(UDP_SAFE_PAYLOAD, frame.size() - offset); // 计算当前分片长度

            VideoPacketHeader hdr{};                 // 定义主机字节序包头
            hdr.magic = VIDEO_MAGIC;                 // 写入协议魔数
            hdr.version = VIDEO_VERSION;             // 写入协议版本
            hdr.msg_type = static_cast<uint8_t>(MsgType::VIDEO_FRAGMENT); // 写入消息类型
            hdr.header_bytes = sizeof(VideoPacketHeader); // 写入包头长度
            hdr.frame_id = frame_id;                 // 写入帧号
            hdr.frag_idx = static_cast<uint16_t>(frag_idx); // 写入分片序号
            hdr.frag_cnt = static_cast<uint16_t>(frag_cnt); // 写入分片总数
            hdr.payload_bytes = static_cast<uint16_t>(payload); // 写入载荷字节数
            hdr.width = camera.width();              // 写入视频宽度
            hdr.height = camera.height();            // 写入视频高度
            hdr.pixel_format = camera.pixel_format(); // 写入当前实际像素格式
            hdr.reserved0 = 0;                       // 预留字段置 0
            hdr.timestamp_us = timestamp_us;         // 写入时间戳

            VideoPacketHeader net_hdr = host_to_net(hdr); // 转换为网络字节序包头
            std::vector<uint8_t> packet(sizeof(VideoPacketHeader) + payload, 0); // 分配完整 UDP 包缓冲区
            std::memcpy(packet.data(), &net_hdr, sizeof(net_hdr)); // 拷贝包头到缓冲区
            std::memcpy(packet.data() + sizeof(VideoPacketHeader), frame.data() + offset, payload); // 拷贝分片载荷

            if (send_bytes(fd_pynq, packet.data(), packet.size(), to_310b) < 0) { // 发送分片到 310B
                std::cout << "[PYNQ] send frag failed frame=" << frame_id << " frag=" << frag_idx << std::endl; // 打印失败日志
                ++frame_failed_pkt;                  // 当前帧失败分片递增
                ++send_fail_cnt;                     // 总失败分片递增
            } else {                                 // 发送成功分支
                ++sent_pkt_cnt;                      // 发送分片计数递增
                ++frame_sent_pkt;                    // 当前帧成功分片递增
            }
        }
        ++sent_frame_cnt;                            // 当前帧处理完成，累计发送帧递增

        bench_log.log_raw("frame_sent",
            "\"frame_id\":" + std::to_string(frame_id) +
            ",\"source_timestamp_us\":" + std::to_string(timestamp_us) +
            ",\"frame_bytes\":" + std::to_string(frame.size()) +
            ",\"width\":" + std::to_string(camera.width()) +
            ",\"height\":" + std::to_string(camera.height()) +
            ",\"pixel_format\":" + std::to_string(static_cast<int>(camera.pixel_format())) +
            ",\"frag_cnt\":" + std::to_string(frag_cnt) +
            ",\"sent_frag_cnt\":" + std::to_string(frame_sent_pkt) +
            ",\"failed_frag_cnt\":" + std::to_string(frame_failed_pkt) +
            ",\"total_sent_pkt\":" + std::to_string(sent_pkt_cnt) +
            ",\"total_send_fail_pkt\":" + std::to_string(send_fail_cnt));

        if (frame_id % 30 == 0) {                   // 每 30 帧打印一次发送状态
            std::cout << "[PYNQ] send frame=" << frame_id << " bytes=" << frame.size()
                      << " fmt=" << static_cast<int>(camera.pixel_format())
                      << " frag_cnt=" << frag_cnt
                      << " total_pkt=" << sent_pkt_cnt << std::endl; // 打印发送统计
            bench_log.log_raw("pynq_stats",
                "\"frame_id\":" + std::to_string(frame_id) +
                ",\"sent_frame_cnt\":" + std::to_string(sent_frame_cnt) +
                ",\"total_sent_pkt\":" + std::to_string(sent_pkt_cnt) +
                ",\"total_send_fail_pkt\":" + std::to_string(send_fail_cnt));
        }

        std::vector<uint8_t> control_packet;         // 定义控制消息缓冲区
        sockaddr_in src{};                           // 定义控制消息源地址
        socklen_t srclen = sizeof(src);              // 定义源地址长度
        int ctrl_n = recv_bytes(fd_pynq, &control_packet, &src, &srclen); // 非阻塞轮询控制消息
        if (ctrl_n > 0) {                            // 收到控制消息分支
            std::string ctrl_text(control_packet.begin(), control_packet.end()); // 将控制包按文本展示
            std::cout << "[PYNQ] recv control: " << ctrl_text << std::endl; // 打印控制日志
            bench_log.log_raw("control_received",
                "\"frame_id\":" + std::to_string(frame_id) +
                ",\"bytes\":" + std::to_string(ctrl_n) +
                ",\"text\":\"" + json_escape(ctrl_text) + "\"");
        }

        ++frame_id;                                  // 帧号自增
        next_send_tp += frame_period;                // 更新下一帧发送时刻
        auto after_send_now = std::chrono::steady_clock::now(); // 获取发送完成后的当前时间
        if (after_send_now > next_send_tp + frame_period) { // 若发送严重落后，避免持续累计延迟
            next_send_tp = after_send_now;           // 将下一帧时刻重置为当前时刻
        }
    }                                                // 结束循环

    std::cout << "[PYNQ] stop send loop" << std::endl; // 打印退出信息
    bench_log.log_raw("pynq_stop",
        "\"sent_frame_cnt\":" + std::to_string(sent_frame_cnt) +
        ",\"total_sent_pkt\":" + std::to_string(sent_pkt_cnt) +
        ",\"total_send_fail_pkt\":" + std::to_string(send_fail_cnt));

    close(fd_pynq);                                  // 关闭 socket
    return 0;                                        // 返回成功码
}                                                    // 主函数结束
