#include <zmq.hpp>
#include <opencv2/opencv.hpp>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>

// 单帧在进程内队列中的表示：
// - id: 连续帧号，便于检测乱序/丢帧
// - timestamp: 发送端打点，用于估算端到端时延
// - data: JPEG 压缩后的字节流
struct FramePacket {
    uint32_t id = 0;
    double timestamp = 0.0;
    std::vector<uint8_t> data;
};

// ================= 发送端类 =================
class ZmqVideoSender {
public:
    // 初始化发送 socket。当前使用 PUSH 模式单向推流。
    void init(const std::string& ip, int port) {
        ctx_ = std::make_unique<zmq::context_t>(1);
        sock_ = std::make_unique<zmq::socket_t>(*ctx_, ZMQ_PUSH);
        sock_->connect("tcp://" + ip + ":" + std::to_string(port));
        // 低延迟调优
        sock_->set(zmq::sockopt::sndhwm, static_cast<int>(MAX_QUEUE));
        sock_->set(zmq::sockopt::linger, 0);
    }

    void start() {
        running_ = true;
        worker_ = std::thread(&ZmqVideoSender::send_loop, this);
    }

    // 主循环只负责入队，避免网络发送阻塞采集线程。
    // 队列满时丢弃最旧帧，优先保证“新鲜度”。
    void push_frame(const cv::Mat& frame, double ts, uint32_t id) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.size() >= MAX_QUEUE) queue_.pop();
        
        FramePacket pkt;
        pkt.id = id;
        pkt.timestamp = ts;
        cv::imencode(".jpg", frame, pkt.data, {cv::IMWRITE_JPEG_QUALITY, 80});
        queue_.push(std::move(pkt));
        cv_.notify_one();
    }

    void stop() {
        running_ = false;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        std::cout << "[ZMQ Sender] Thread exited safely.\n";
    }

private:
    // 后台发送线程：从队列取帧 -> 组包 -> 非阻塞发送。
    void send_loop() {
        while (running_) {
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) break;

            FramePacket pkt = std::move(queue_.front());
            queue_.pop();
            lock.unlock();

            // 组装报文: [4B ID][8B TS][JPEG Data]
            std::vector<uint8_t> payload(12 + pkt.data.size());
            std::memcpy(payload.data(), &pkt.id, 4);
            std::memcpy(payload.data() + 4, &pkt.timestamp, 8);
            std::memcpy(payload.data() + 12, pkt.data.data(), pkt.data.size());

            // dontwait 避免发送线程被下游阻塞卡死。
            sock_->send(zmq::buffer(payload.data(), payload.size()), zmq::send_flags::dontwait);
        }
    }

    std::unique_ptr<zmq::context_t> ctx_;
    std::unique_ptr<zmq::socket_t> sock_;
    std::queue<FramePacket> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread worker_;
    bool running_ = false;
    static constexpr size_t MAX_QUEUE = 8;
};

// ================= 接收端类 =================
class ZmqVideoReceiver {
public:
    // 初始化接收 socket。当前使用 PULL 端绑定端口。
    void init(int port) {
        ctx_ = std::make_unique<zmq::context_t>(1);
        sock_ = std::make_unique<zmq::socket_t>(*ctx_, ZMQ_PULL);
        sock_->bind("tcp://*:" + std::to_string(port));
        sock_->set(zmq::sockopt::rcvhwm, static_cast<int>(MAX_QUEUE));
        sock_->set(zmq::sockopt::linger, 0);
    }

    void start() {
        running_ = true;
        worker_ = std::thread(&ZmqVideoReceiver::recv_loop, this);
    }

    // 主线程从队列取一帧并解码。超时返回 false，便于 UI/业务循环继续推进。
    bool pop_frame(cv::Mat& out_frame, double& ts, uint32_t& id, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]{ return !queue_.empty(); })) {
            FramePacket pkt = std::move(queue_.front());
            queue_.pop();
            lock.unlock();

            out_frame = cv::imdecode(cv::Mat(pkt.data), cv::IMREAD_COLOR);
            ts = pkt.timestamp;
            id = pkt.id;
            return true;
        }
        return false;
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        std::cout << "[ZMQ Receiver] Thread exited safely.\n";
    }

private:
    // 后台接收线程：非阻塞 recv，收到后解析头部并入队。
    void recv_loop() {
        while (running_) {
            zmq::message_t msg;
            auto res = sock_->recv(msg, zmq::recv_flags::dontwait);
            if (res) {
                const uint8_t* data = static_cast<const uint8_t*>(msg.data());
                size_t size = msg.size();
                if (size >= 12) {
                    FramePacket pkt;
                    std::memcpy(&pkt.id, data, 4);
                    std::memcpy(&pkt.timestamp, data + 4, 8);
                    pkt.data.assign(data + 12, data + size);

                    std::lock_guard<std::mutex> lock(mtx_);
                    // 与发送端一致：拥塞时丢最旧帧，降低显示端累计延迟。
                    if (queue_.size() >= MAX_QUEUE) queue_.pop();
                    queue_.push(std::move(pkt));
                    cv_.notify_one();
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    std::unique_ptr<zmq::context_t> ctx_;
    std::unique_ptr<zmq::socket_t> sock_;
    std::queue<FramePacket> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread worker_;
    bool running_ = false;
    static constexpr size_t MAX_QUEUE = 8;
};