#include "udp_common.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/select.h>

using namespace udp_net;

namespace {
volatile std::sig_atomic_t g_stop = 0;

void handle_signal(int) {
    g_stop = 1;
}

struct Options {
    std::string bind_ip = PC_IP;
    int video_port = PC_PORT;
    int result_port = PC_CTRL_PORT;
    int video_ctrl_port = PC_VIDEO_CTRL_PORT;
    std::string output = "output.mp4";
};

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string* out) {
            if (i + 1 < argc) {
                *out = argv[++i];
            }
        };
        auto next_int = [&](int* out) {
            if (i + 1 < argc) {
                *out = std::stoi(argv[++i]);
            }
        };

        if (arg == "--bind-ip") {
            next(&opt.bind_ip);
        } else if (arg == "--video-port") {
            next_int(&opt.video_port);
        } else if (arg == "--result-port") {
            next_int(&opt.result_port);
        } else if (arg == "--video-ctrl-port") {
            next_int(&opt.video_ctrl_port);
        } else if (arg == "--output") {
            next(&opt.output);
        }
    }
    return opt;
}

struct FrameAssembler {
    uint32_t frame_id = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t pixel_format = PIX_FMT_RGB24;
    uint16_t frag_cnt = 0;
    std::vector<uint8_t> frame_data;
    std::vector<uint8_t> frag_recv;
    std::vector<uint16_t> frag_sizes;
    uint16_t recv_cnt = 0;
    std::chrono::steady_clock::time_point start_tp;
};

constexpr std::size_t kMaxInflightFrames = 64;

bool init_new_frame(FrameAssembler* as, const VideoPacketHeader& hdr) {
    if (!as || hdr.width == 0 || hdr.height == 0 || hdr.frag_cnt == 0) {
        return false;
    }

    const std::size_t frame_bytes = static_cast<std::size_t>(hdr.frag_cnt) * UDP_SAFE_PAYLOAD;
    as->frame_id = hdr.frame_id;
    as->width = hdr.width;
    as->height = hdr.height;
    as->pixel_format = hdr.pixel_format;
    as->frag_cnt = hdr.frag_cnt;
    as->frame_data.assign(frame_bytes, 0);
    as->frag_recv.assign(hdr.frag_cnt, 0);
    as->frag_sizes.assign(hdr.frag_cnt, 0);
    as->recv_cnt = 0;
    as->start_tp = std::chrono::steady_clock::now();
    return true;
}

bool find_jpeg_span(const std::vector<uint8_t>& data, std::size_t size, std::size_t* start, std::size_t* length) {
    if (!start || !length || size < 4 || data.size() < size) {
        return false;
    }

    std::size_t soi = size;
    for (std::size_t i = 0; i + 1 < size; ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0xD8) {
            soi = i;
            break;
        }
    }
    if (soi == size) {
        return false;
    }

    std::size_t eoi = size;
    for (std::size_t i = size - 1; i > soi; --i) {
        if (data[i - 1] == 0xFF && data[i] == 0xD9) {
            eoi = i + 1;
            break;
        }
    }
    if (eoi == size || eoi <= soi + 2) {
        return false;
    }

    *start = soi;
    *length = eoi - soi;
    return true;
}

std::FILE* start_ffmpeg_pipe(uint16_t width, uint16_t height, uint8_t pixel_format, const std::string& output_file) {
    std::ostringstream cmd;
    if (pixel_format == PIX_FMT_MJPEG) {
        cmd << "ffmpeg -y -f image2pipe -vcodec mjpeg -framerate " << VIDEO_FPS
            << " -i - -an -c:v mpeg4 -q:v 5 " << output_file;
    } else if (pixel_format == PIX_FMT_NV12) {
        cmd << "ffmpeg -y -f rawvideo -pixel_format nv12 -video_size " << width << "x" << height
            << " -framerate " << VIDEO_FPS << " -i - -an -c:v mpeg4 -q:v 5 " << output_file;
    } else {
        cmd << "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size " << width << "x" << height
            << " -framerate " << VIDEO_FPS << " -i - -an -c:v mpeg4 -q:v 5 " << output_file;
    }

    std::cout << "[PC] start ffmpeg: " << cmd.str() << std::endl;
    std::FILE* pipe = popen(cmd.str().c_str(), "w");
    if (!pipe) {
        std::cerr << "[PC] popen ffmpeg failed" << std::endl;
    }
    return pipe;
}

int create_and_bind(const std::string& ip, int port) {
    int fd = create_socket();
    if (fd < 0) {
        return -1;
    }
    if (!bind_ip_port(fd, ip, port)) {
        close(fd);
        return -1;
    }
    tune_socket_buffers(fd, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES);
    return fd;
}

void cleanup_timeouts(std::unordered_map<uint32_t, FrameAssembler>* inflight, uint64_t* drop_frame_cnt) {
    if (!inflight || inflight->empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::vector<uint32_t> timeout_ids;
    timeout_ids.reserve(inflight->size());
    for (const auto& kv : *inflight) {
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - kv.second.start_tp).count();
        if (elapsed_ms > FRAME_TIMEOUT_MS) {
            timeout_ids.push_back(kv.first);
        }
    }
    for (uint32_t id : timeout_ids) {
        ++(*drop_frame_cnt);
        std::cout << "[PC] drop frame timeout frame_id=" << id << std::endl;
        inflight->erase(id);
    }
}

bool handle_video_packet(const std::vector<uint8_t>& packet,
                         std::unordered_map<uint32_t, FrameAssembler>* inflight,
                         std::FILE** ffmpeg_pipe,
                         uint8_t* ffmpeg_input_format,
                         const std::string& output,
                         uint64_t* ok_frame_cnt,
                         uint64_t* drop_frame_cnt) {
    if (!inflight || !ffmpeg_pipe || !ffmpeg_input_format || !ok_frame_cnt || !drop_frame_cnt) {
        return true;
    }

    VideoPacketHeader hdr{};
    if (!parse_video_header(packet, &hdr)) {
        return true;
    }

    auto it = inflight->find(hdr.frame_id);
    if (it == inflight->end()) {
        if (inflight->size() >= kMaxInflightFrames) {
            uint32_t oldest_id = 0;
            bool has_oldest = false;
            auto oldest_tp = std::chrono::steady_clock::now();
            for (const auto& kv : *inflight) {
                if (!has_oldest || kv.second.start_tp < oldest_tp) {
                    has_oldest = true;
                    oldest_id = kv.first;
                    oldest_tp = kv.second.start_tp;
                }
            }
            if (has_oldest) {
                ++(*drop_frame_cnt);
                std::cout << "[PC] drop overflow frame frame_id=" << oldest_id << std::endl;
                inflight->erase(oldest_id);
            }
        }

        FrameAssembler fresh;
        if (!init_new_frame(&fresh, hdr)) {
            return true;
        }
        it = inflight->emplace(hdr.frame_id, std::move(fresh)).first;
    }

    FrameAssembler& assembler = it->second;

    if (hdr.frag_cnt != assembler.frag_cnt || hdr.width != assembler.width ||
        hdr.height != assembler.height || hdr.pixel_format != assembler.pixel_format) {
        ++(*drop_frame_cnt);
        std::cout << "[PC] drop frame meta mismatch frame_id=" << hdr.frame_id << std::endl;
        inflight->erase(hdr.frame_id);
        return true;
    }

    if (hdr.frag_idx >= assembler.frag_cnt || hdr.payload_bytes > UDP_SAFE_PAYLOAD) {
        return true;
    }

    const std::size_t payload_offset = sizeof(VideoPacketHeader);
    const std::size_t target_offset = static_cast<std::size_t>(hdr.frag_idx) * UDP_SAFE_PAYLOAD;
    if (target_offset + hdr.payload_bytes > assembler.frame_data.size()) {
        ++(*drop_frame_cnt);
        std::cout << "[PC] drop frame out-of-range frame_id=" << hdr.frame_id << std::endl;
        inflight->erase(hdr.frame_id);
        return true;
    }

    if (!assembler.frag_recv[hdr.frag_idx]) {
        std::memcpy(assembler.frame_data.data() + target_offset, packet.data() + payload_offset, hdr.payload_bytes);
        assembler.frag_recv[hdr.frag_idx] = 1;
        assembler.frag_sizes[hdr.frag_idx] = hdr.payload_bytes;
        ++assembler.recv_cnt;
    }

    if (assembler.recv_cnt != assembler.frag_cnt) {
        return true;
    }

    std::size_t frame_bytes = 0;
    for (uint16_t i = 0; i < assembler.frag_cnt; ++i) {
        frame_bytes += assembler.frag_sizes[i];
    }

    if (assembler.pixel_format == PIX_FMT_RGB24) {
        const std::size_t expected = static_cast<std::size_t>(assembler.width) * static_cast<std::size_t>(assembler.height) * 3;
        if (frame_bytes != expected) {
            ++(*drop_frame_cnt);
            std::cout << "[PC] drop RGB24 frame size mismatch bytes=" << frame_bytes
                      << " expected=" << expected << std::endl;
            inflight->erase(hdr.frame_id);
            return true;
        }
    } else if (assembler.pixel_format == PIX_FMT_NV12) {
        const std::size_t expected = static_cast<std::size_t>(assembler.width) * static_cast<std::size_t>(assembler.height) * 3 / 2;
        if (frame_bytes != expected) {
            ++(*drop_frame_cnt);
            std::cout << "[PC] drop NV12 frame size mismatch bytes=" << frame_bytes
                      << " expected=" << expected << std::endl;
            inflight->erase(hdr.frame_id);
            return true;
        }
    }

    std::size_t write_offset = 0;
    std::size_t write_size = frame_bytes;
    if (assembler.pixel_format == PIX_FMT_MJPEG) {
        if (!find_jpeg_span(assembler.frame_data, frame_bytes, &write_offset, &write_size)) {
            ++(*drop_frame_cnt);
            std::cout << "[PC] drop invalid JPEG frame frame_id=" << hdr.frame_id << std::endl;
            inflight->erase(hdr.frame_id);
            return true;
        }
    }

    if (!*ffmpeg_pipe) {
        *ffmpeg_pipe = start_ffmpeg_pipe(assembler.width, assembler.height, assembler.pixel_format, output);
        if (!*ffmpeg_pipe) {
            return false;
        }
        *ffmpeg_input_format = assembler.pixel_format;
    }

    if (assembler.pixel_format != *ffmpeg_input_format) {
        ++(*drop_frame_cnt);
        std::cout << "[PC] drop format switch frame_id=" << hdr.frame_id << std::endl;
        inflight->erase(hdr.frame_id);
        return true;
    }

    const std::size_t written = std::fwrite(assembler.frame_data.data() + write_offset, 1, write_size, *ffmpeg_pipe);
    if (written != write_size) {
        std::cerr << "[PC] write ffmpeg pipe failed" << std::endl;
        return false;
    }
    std::fflush(*ffmpeg_pipe);

    ++(*ok_frame_cnt);
    if (*ok_frame_cnt % 30 == 0) {
        std::cout << "[PC] video_ok=" << *ok_frame_cnt << " video_drop=" << *drop_frame_cnt
                  << " inflight=" << inflight->size() << std::endl;
    }

    inflight->erase(hdr.frame_id);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const Options opt = parse_args(argc, argv);

    int fd_video = create_and_bind(opt.bind_ip, opt.video_port);
    if (fd_video < 0) {
        return 1;
    }
    int fd_result = create_and_bind(opt.bind_ip, opt.result_port);
    if (fd_result < 0) {
        close(fd_video);
        return 1;
    }
    int fd_video_ctrl = create_and_bind(opt.bind_ip, opt.video_ctrl_port);
    if (fd_video_ctrl < 0) {
        close(fd_result);
        close(fd_video);
        return 1;
    }

    std::cout << "[PC] listen ip=" << opt.bind_ip
              << " video=" << opt.video_port
              << " result=" << opt.result_port
              << " video_ctrl=" << opt.video_ctrl_port
              << " output=" << opt.output << std::endl;

    std::unordered_map<uint32_t, FrameAssembler> inflight;
    std::FILE* ffmpeg_pipe = nullptr;
    uint8_t ffmpeg_input_format = 0;

    uint64_t video_ok = 0;
    uint64_t video_drop = 0;
    uint64_t result_cnt = 0;
    uint64_t ctrl_cnt = 0;

    while (!g_stop) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_video, &rfds);
        FD_SET(fd_result, &rfds);
        FD_SET(fd_video_ctrl, &rfds);
        const int max_fd = std::max(fd_video, std::max(fd_result, fd_video_ctrl));

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 20000;
        const int ready = select(max_fd + 1, &rfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[PC] select failed: " << std::strerror(errno) << std::endl;
            break;
        }

        cleanup_timeouts(&inflight, &video_drop);
        if (ready == 0) {
            continue;
        }

        if (FD_ISSET(fd_video, &rfds)) {
            std::vector<uint8_t> packet;
            sockaddr_in src{};
            socklen_t srclen = sizeof(src);
            int n = recv_bytes(fd_video, &packet, &src, &srclen);
            if (n > 0) {
                if (!handle_video_packet(packet, &inflight, &ffmpeg_pipe, &ffmpeg_input_format,
                                         opt.output, &video_ok, &video_drop)) {
                    break;
                }
            }
        }

        if (FD_ISSET(fd_result, &rfds)) {
            std::string msg = recv_msg(fd_result);
            if (!msg.empty()) {
                ++result_cnt;
                std::cout << "[PC][result] " << msg << std::endl;
            }
        }

        if (FD_ISSET(fd_video_ctrl, &rfds)) {
            std::string msg = recv_msg(fd_video_ctrl);
            if (!msg.empty()) {
                ++ctrl_cnt;
                std::cout << "[PC][video_ctrl] " << msg << std::endl;
            }
        }
    }

    if (ffmpeg_pipe) {
        std::fflush(ffmpeg_pipe);
        int rc = pclose(ffmpeg_pipe);
        std::cout << "[PC] ffmpeg exit code=" << rc << std::endl;
    }

    std::cout << "[PC] stop video_ok=" << video_ok
              << " video_drop=" << video_drop
              << " result_cnt=" << result_cnt
              << " ctrl_cnt=" << ctrl_cnt << std::endl;

    close(fd_video_ctrl);
    close(fd_result);
    close(fd_video);
    return 0;
}
