#include "zmq_streamer.hpp"
#include <chrono>

int main() {
    // 初始化接收模块：后台线程收包，主线程取帧显示。
    ZmqVideoReceiver receiver;
    receiver.init(5555);
    receiver.start(); // 启动后台网络线程

    std::cout << "[PC Main] Waiting for frames from ZMQ thread...\n";
    cv::namedWindow("PC_Receiver", cv::WINDOW_NORMAL);

    while (true) {
        cv::Mat frame;
        double send_ts = 0;
        uint32_t frame_id = 0;

        // 从线程安全队列拉帧；超时时继续循环，避免界面卡死。
        if (!receiver.pop_frame(frame, send_ts, frame_id, 100)) continue;

        // 用发送端时间戳粗略估算 Net + Decode 时延。
        auto now = std::chrono::system_clock::now();
        double recv_ts = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() / 1e6;
        double latency_ms = (recv_ts - send_ts) * 1000.0;

        std::string info = "Net+Decode: " + std::to_string((int)latency_ms) + "ms | ID: " + std::to_string(frame_id);
        cv::putText(frame, info, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        cv::imshow("PC_Receiver", frame);

        if (cv::waitKey(1) == 'q') break;
    }

    receiver.stop();
    cv::destroyAllWindows();
    return 0;
}