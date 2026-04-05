#define _POSIX_C_SOURCE 199309L

#include <zmq.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "video_stream.h"

struct ReceiverConfig {
    std::string connect_addr = "tcp://127.0.0.1:5555";
    bool show = true;
    int rcv_hwm = 2;
};

static inline uint64_t get_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static bool parse_int(const char *text, int &value) {
    if (!text) {
        return false;
    }
    char *end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    value = (int)parsed;
    return true;
}

static void print_usage(const char *name) {
    std::fprintf(stderr,
                 "Usage: %s [--connect tcp://IP:PORT] [--show 0|1] [--hwm N]\n",
                 name);
}

int main(int argc, char **argv) {
    ReceiverConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--connect") == 0 && i + 1 < argc) {
            cfg.connect_addr = argv[++i];
        } else if (std::strcmp(arg, "--show") == 0 && i + 1 < argc) {
            int flag = 0;
            if (!parse_int(argv[++i], flag)) {
                print_usage(argv[0]);
                return 1;
            }
            cfg.show = (flag != 0);
        } else if (std::strcmp(arg, "--hwm") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.rcv_hwm)) {
                print_usage(argv[0]);
                return 1;
            }
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (cfg.rcv_hwm < 1) {
        cfg.rcv_hwm = 1;
    }

    void *ctx = zmq_ctx_new();
    if (!ctx) {
        std::fprintf(stderr, "[LAPTOP] zmq_ctx_new failed\n");
        return 1;
    }

    void *sub = zmq_socket(ctx, ZMQ_SUB);
    if (!sub) {
        std::fprintf(stderr, "[LAPTOP] zmq_socket failed\n");
        zmq_ctx_destroy(ctx);
        return 1;
    }

    int zero = 0;
    zmq_setsockopt(sub, ZMQ_LINGER, &zero, sizeof(zero));
    zmq_setsockopt(sub, ZMQ_RCVHWM, &cfg.rcv_hwm, sizeof(cfg.rcv_hwm));
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);

    if (zmq_connect(sub, cfg.connect_addr.c_str()) != 0) {
        std::fprintf(stderr,
                     "[LAPTOP] connect failed on %s: %s\n",
                     cfg.connect_addr.c_str(),
                     zmq_strerror(zmq_errno()));
        zmq_close(sub);
        zmq_ctx_destroy(ctx);
        return 1;
    }

    std::printf("[LAPTOP] subscriber ready on %s, show=%d, hwm=%d\n",
                cfg.connect_addr.c_str(), cfg.show ? 1 : 0, cfg.rcv_hwm);

    std::vector<uint8_t> buffer(VS_MAX_FRAME_BYTES + sizeof(vs_frame_header_t));
    cv::Mat yuv;
    cv::Mat bgr;

    uint64_t stat_window_start_ns = get_ns();
    uint32_t stat_frames = 0;
    double stat_latency_sum = 0.0;
    uint32_t drop_or_invalid = 0;
    uint32_t last_frame_id = 0;
    bool has_last = false;

    while (true) {
        int size = zmq_recv(sub, buffer.data(), buffer.size(), 0);
        if (size < (int)sizeof(vs_frame_header_t)) {
            ++drop_or_invalid;
            continue;
        }

        vs_frame_header_t header{};
        std::memcpy(&header, buffer.data(), sizeof(header));

        if (header.magic != VS_MAGIC || header.version != VS_VERSION) {
            ++drop_or_invalid;
            continue;
        }
        if (header.pixel_format != VS_FMT_YUV420P8) {
            ++drop_or_invalid;
            continue;
        }

        const int expected_payload = (int)header.width * (int)header.height * 3 / 2;
        if (header.width < 2 || header.height < 2 || expected_payload <= 0) {
            ++drop_or_invalid;
            continue;
        }
        if ((int)header.payload_bytes != expected_payload) {
            ++drop_or_invalid;
            continue;
        }

        const int expected_total = (int)sizeof(vs_frame_header_t) + expected_payload;
        if (size < expected_total) {
            ++drop_or_invalid;
            continue;
        }

        const uint8_t *payload = buffer.data() + sizeof(vs_frame_header_t);
        yuv = cv::Mat((int)header.height * 3 / 2, (int)header.width, CV_8UC1, const_cast<uint8_t *>(payload));
        cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_I420);

        uint64_t rx_ns = get_ns();
        double latency_ms = (double)(rx_ns - header.timestamp_ns) / 1000000.0;

        if (cfg.show) {
            cv::putText(bgr,
                        cv::format("frame=%u latency=%.2fms", header.frame_id, latency_ms),
                        cv::Point(12, 28),
                        cv::FONT_HERSHEY_SIMPLEX,
                        0.7,
                        cv::Scalar(0, 255, 0),
                        2,
                        cv::LINE_AA);
            cv::imshow("video_sub_preview", bgr);
            int key = cv::waitKey(1);
            if (key == 27 || key == 'q' || key == 'Q') {
                break;
            }
        }

        if (has_last && header.frame_id != last_frame_id + 1u) {
            drop_or_invalid += (header.frame_id > last_frame_id + 1u) ? (header.frame_id - last_frame_id - 1u) : 1u;
        }
        has_last = true;
        last_frame_id = header.frame_id;

        ++stat_frames;
        stat_latency_sum += latency_ms;

        uint64_t now_ns = get_ns();
        double win_s = (double)(now_ns - stat_window_start_ns) / 1000000000.0;
        if (win_s >= 1.0) {
            double fps = (double)stat_frames / std::max(win_s, 1e-6);
            double avg_latency = stat_frames > 0 ? stat_latency_sum / (double)stat_frames : 0.0;
            std::printf("[LAPTOP] fps=%.2f avg_latency=%.2f ms invalid_or_drop=%u last_frame=%u roi=(%u,%u,%u,%u)\n",
                        fps,
                        avg_latency,
                        drop_or_invalid,
                        header.frame_id,
                        header.roi_x_permille,
                        header.roi_y_permille,
                        header.roi_w_permille,
                        header.roi_h_permille);
            stat_window_start_ns = now_ns;
            stat_frames = 0;
            stat_latency_sum = 0.0;
        }
    }

    zmq_close(sub);
    zmq_ctx_destroy(ctx);
    return 0;
}
