#define _POSIX_C_SOURCE 199309L

#include <zmq.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "video_stream.h"

struct SenderConfig {
    std::string bind_addr = "tcp://*:5555";
    int camera_index = 0;
    int width = 640;
    int height = 480;
    int fps = 30;
    float roi_x = 0.0f;
    float roi_y = 0.0f;
    float roi_w = 1.0f;
    float roi_h = 1.0f;
    float gamma = 2.2f;
    int denoise_ksize = 3;
    int wb_mode = 1;
    bool show = false;
};

static inline uint64_t get_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static bool parse_roi(const std::string &text, float &x, float &y, float &w, float &h) {
    if (std::sscanf(text.c_str(), "%f,%f,%f,%f", &x, &y, &w, &h) != 4) {
        return false;
    }
    return x >= 0.0f && y >= 0.0f && w > 0.0f && h > 0.0f && x + w <= 1.0f && y + h <= 1.0f;
}

static cv::Rect make_even_roi(const SenderConfig &cfg, int frame_width, int frame_height) {
    int x = (int)std::lround(cfg.roi_x * frame_width);
    int y = (int)std::lround(cfg.roi_y * frame_height);
    int w = (int)std::lround(cfg.roi_w * frame_width);
    int h = (int)std::lround(cfg.roi_h * frame_height);

    x = std::clamp(x, 0, std::max(0, frame_width - 2));
    y = std::clamp(y, 0, std::max(0, frame_height - 2));
    w = std::clamp(w, 2, frame_width - x);
    h = std::clamp(h, 2, frame_height - y);

    if (x % 2 != 0) {
        --x;
        ++w;
    }
    if (y % 2 != 0) {
        --y;
        ++h;
    }
    if (w % 2 != 0) {
        --w;
    }
    if (h % 2 != 0) {
        --h;
    }

    w = std::max(2, std::min(w, frame_width - x));
    h = std::max(2, std::min(h, frame_height - y));

    if (w % 2 != 0) {
        --w;
    }
    if (h % 2 != 0) {
        --h;
    }

    return cv::Rect(x, y, w, h);
}

static void apply_gray_world_wb(const cv::Mat &src_bgr, cv::Mat &dst_bgr) {
    cv::Scalar mean_bgr = cv::mean(src_bgr);
    double mean_sum = mean_bgr[0] + mean_bgr[1] + mean_bgr[2] + 1e-6;
    double target = mean_sum / 3.0;
    double gains[3] = {
        target / (mean_bgr[0] + 1e-6),
        target / (mean_bgr[1] + 1e-6),
        target / (mean_bgr[2] + 1e-6),
    };

    cv::Mat work;
    src_bgr.convertTo(work, CV_32FC3);

    for (int y = 0; y < work.rows; ++y) {
        cv::Vec3f *row = work.ptr<cv::Vec3f>(y);
        for (int x = 0; x < work.cols; ++x) {
            row[x][0] = (float)(row[x][0] * gains[0]);
            row[x][1] = (float)(row[x][1] * gains[1]);
            row[x][2] = (float)(row[x][2] * gains[2]);
        }
    }

    work.convertTo(dst_bgr, CV_8UC3);
}

static cv::Mat build_gamma_lut(float gamma) {
    cv::Mat lut(1, 256, CV_8UC1);
    float inv_gamma = 1.0f / std::max(gamma, 0.1f);
    for (int i = 0; i < 256; ++i) {
        float normalized = (float)i / 255.0f;
        float corrected = std::pow(normalized, inv_gamma);
        lut.at<uchar>(0, i) = cv::saturate_cast<uchar>(corrected * 255.0f + 0.5f);
    }
    return lut;
}

static bool parse_int(const char *text, int &value) {
    char *end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (!text || end == text || *end != '\0') {
        return false;
    }
    value = (int)parsed;
    return true;
}

static bool parse_float(const char *text, float &value) {
    char *end = nullptr;
    float parsed = std::strtof(text, &end);
    if (!text || end == text || *end != '\0') {
        return false;
    }
    value = parsed;
    return true;
}

static void print_usage(const char *name) {
    std::fprintf(stderr,
                 "Usage: %s [--bind addr] [--camera N] [--width W] [--height H] [--fps N]\\n"
                 "       [--roi x,y,w,h] [--gamma G] [--denoise K] [--show 0|1]\\n",
                 name);
}

int main(int argc, char **argv) {
    SenderConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--bind") == 0 && i + 1 < argc) {
            cfg.bind_addr = argv[++i];
        } else if (std::strcmp(arg, "--camera") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.camera_index)) {
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(arg, "--width") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.width)) {
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(arg, "--height") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.height)) {
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(arg, "--fps") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.fps)) {
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(arg, "--roi") == 0 && i + 1 < argc) {
            if (!parse_roi(argv[++i], cfg.roi_x, cfg.roi_y, cfg.roi_w, cfg.roi_h)) {
                std::fprintf(stderr, "[PYNQ] invalid roi, expected x,y,w,h in [0,1]\\n");
                return 1;
            }
        } else if (std::strcmp(arg, "--gamma") == 0 && i + 1 < argc) {
            if (!parse_float(argv[++i], cfg.gamma)) {
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(arg, "--denoise") == 0 && i + 1 < argc) {
            if (!parse_int(argv[++i], cfg.denoise_ksize)) {
                print_usage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(arg, "--show") == 0 && i + 1 < argc) {
            int show_flag = 0;
            if (!parse_int(argv[++i], show_flag)) {
                print_usage(argv[0]);
                return 1;
            }
            cfg.show = (show_flag != 0);
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (cfg.denoise_ksize < 3) {
        cfg.denoise_ksize = 3;
    }
    if (cfg.denoise_ksize % 2 == 0) {
        ++cfg.denoise_ksize;
    }

    cv::VideoCapture capture(cfg.camera_index, cv::CAP_V4L2);
    if (!capture.isOpened()) {
        std::fprintf(stderr, "[PYNQ] failed to open camera %d\\n", cfg.camera_index);
        return 1;
    }

    capture.set(cv::CAP_PROP_FRAME_WIDTH, cfg.width);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, cfg.height);
    capture.set(cv::CAP_PROP_FPS, cfg.fps);
    capture.set(cv::CAP_PROP_BUFFERSIZE, 1);

    void *ctx = zmq_ctx_new();
    if (!ctx) {
        std::fprintf(stderr, "[PYNQ] zmq_ctx_new failed\\n");
        return 1;
    }

    void *pub = zmq_socket(ctx, ZMQ_PUB);
    if (!pub) {
        std::fprintf(stderr, "[PYNQ] zmq_socket failed\\n");
        zmq_ctx_destroy(ctx);
        return 1;
    }

    int zero = 0;
    int hwm = 2;
    zmq_setsockopt(pub, ZMQ_LINGER, &zero, sizeof(zero));
    zmq_setsockopt(pub, ZMQ_SNDHWM, &hwm, sizeof(hwm));

    if (zmq_bind(pub, cfg.bind_addr.c_str()) != 0) {
        std::fprintf(stderr, "[PYNQ] bind failed on %s: %s\\n", cfg.bind_addr.c_str(), zmq_strerror(zmq_errno()));
        zmq_close(pub);
        zmq_ctx_destroy(ctx);
        return 1;
    }

    std::printf("[PYNQ] camera %d opened, bind %s\\n", cfg.camera_index, cfg.bind_addr.c_str());
    std::printf("[PYNQ] target %dx%d @ %dfps, roi=%.3f,%.3f,%.3f,%.3f gamma=%.2f denoise=%d\\n",
                cfg.width, cfg.height, cfg.fps, cfg.roi_x, cfg.roi_y, cfg.roi_w, cfg.roi_h, cfg.gamma, cfg.denoise_ksize);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    cv::Mat frame_bgr;
    cv::Mat roi_bgr;
    cv::Mat wb_bgr;
    cv::Mat denoised_bgr;
    cv::Mat gamma_bgr;
    cv::Mat yuv420;
    cv::Mat gamma_lut = build_gamma_lut(cfg.gamma);

    std::vector<uint8_t> send_buffer(sizeof(vs_frame_header_t) + (size_t)cfg.width * (size_t)cfg.height * 3u / 2u + 1024u);
    uint32_t frame_id = 0;

    while (true) {
        uint64_t capture_start_ns = get_ns();
        if (!capture.read(frame_bgr) || frame_bgr.empty()) {
            std::fprintf(stderr, "[PYNQ] camera read failed\\n");
            continue;
        }

        cv::Rect roi = make_even_roi(cfg, frame_bgr.cols, frame_bgr.rows);
        roi_bgr = frame_bgr(roi).clone();

        apply_gray_world_wb(roi_bgr, wb_bgr);
        cv::medianBlur(wb_bgr, denoised_bgr, cfg.denoise_ksize);
        cv::LUT(denoised_bgr, gamma_lut, gamma_bgr);
        cv::cvtColor(gamma_bgr, yuv420, cv::COLOR_BGR2YUV_I420);

        if (!yuv420.isContinuous()) {
            yuv420 = yuv420.clone();
        }

        const uint32_t payload_bytes = (uint32_t)yuv420.total() * (uint32_t)yuv420.elemSize();
        const size_t total_bytes = sizeof(vs_frame_header_t) + payload_bytes;
        if (send_buffer.size() < total_bytes) {
            send_buffer.resize(total_bytes);
        }

        vs_frame_header_t header{};
        header.magic = VS_MAGIC;
        header.version = VS_VERSION;
        header.header_bytes = (uint16_t)sizeof(vs_frame_header_t);
        header.frame_id = frame_id;
        header.timestamp_ns = capture_start_ns;
        header.width = (uint16_t)roi.width;
        header.height = (uint16_t)roi.height;
        header.roi_x_permille = (uint16_t)std::lround(cfg.roi_x * 1000.0f);
        header.roi_y_permille = (uint16_t)std::lround(cfg.roi_y * 1000.0f);
        header.roi_w_permille = (uint16_t)std::lround(cfg.roi_w * 1000.0f);
        header.roi_h_permille = (uint16_t)std::lround(cfg.roi_h * 1000.0f);
        header.pixel_format = VS_FMT_YUV420P8;
        header.white_balance_mode = (uint16_t)cfg.wb_mode;
        header.denoise_mode = (uint16_t)cfg.denoise_ksize;
        header.gamma_permille = (uint16_t)std::lround(cfg.gamma * 1000.0f);
        header.payload_bytes = payload_bytes;
        header.crc32 = 0;

        std::memcpy(send_buffer.data(), &header, sizeof(header));
        std::memcpy(send_buffer.data() + sizeof(header), yuv420.data, payload_bytes);

        int rc = zmq_send(pub, send_buffer.data(), total_bytes, 0);
        if (rc < 0) {
            std::fprintf(stderr, "[PYNQ] send failed: %s\\n", zmq_strerror(zmq_errno()));
            break;
        }

        if (cfg.show) {
            cv::imshow("pynq_roi_preview", gamma_bgr);
            if (cv::waitKey(1) == 27) {
                break;
            }
        }

        if ((frame_id % 30u) == 0u) {
            uint64_t now_ns = get_ns();
            double proc_ms = (double)(now_ns - capture_start_ns) / 1000000.0;
            std::printf("[PYNQ] frame=%u roi=%dx%d payload=%u proc=%.2f ms\\n",
                        frame_id, roi.width, roi.height, payload_bytes, proc_ms);
        }

        ++frame_id;
    }

    zmq_close(pub);
    zmq_ctx_destroy(ctx);
    return 0;
}
