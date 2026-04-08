#include "zmq_streamer.hpp"
#include <chrono>
#include <csignal>

// 通过 Ctrl+C 触发优雅退出，保证线程和摄像头资源正确释放。
volatile sig_atomic_t g_running = 1;
void signal_handler(int) { g_running = 0; }

int main() {
    std::signal(SIGINT, signal_handler);

    // 初始化网络发送模块：主线程采集，后台线程发送。
    ZmqVideoSender sender;
    sender.init("192.168.2.100", 5555);
    sender.start(); // 🚀 启动后台网络线程

    // 采集端固定从本地摄像头读取。
    cv::VideoCapture cap(0);
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 640);
    cap.set(cv::CAP_PROP_FPS, 30);
    if (!cap.isOpened()) { std::cerr << "❌ Camera open failed\n"; return -1; }

    std::cout << "[PYNQ Main] Camera ready. Pushing to ZMQ thread...\n";
    uint32_t frame_id = 0;

    while (g_running) {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) break;

        // 这里是预处理占位点：后续可替换为 FPGA/HLS 输出。
        cv::Mat resized;
        cv::resize(frame, resized, cv::Size(640, 640));

        // 发送时间戳用于接收端估算端到端延迟。
        auto now = std::chrono::system_clock::now();
        double ts = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() / 1e6;

        // 仅入队，不阻塞主循环
        sender.push_frame(resized, ts, frame_id++);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    sender.stop();
    cap.release();
    return 0;
}