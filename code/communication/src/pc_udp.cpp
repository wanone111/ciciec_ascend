#include "udp_common.h"                             // 引入 UDP 公共工具头文件
#include <chrono>                                    // 引入时间工具
#include <csignal>                                   // 引入信号处理
#include <cstdio>                                    // 引入 popen/pclose 和 fwrite
#include <iostream>                                  // 引入标准输出流
#include <sstream>                                   // 引入字符串流
#include <unordered_map>                             // 引入哈希表容器
#include <vector>                                    // 引入二进制容器

using namespace udp_net;                             // 使用 udp_net 命名空间

namespace {                                          // 匿名命名空间，限制内部符号可见范围
volatile std::sig_atomic_t g_stop = 0;               // 程序停止标志，由信号处理函数修改

void handle_signal(int) {                            // 信号处理函数
    g_stop = 1;                                      // 收到中断信号后标记退出
}

struct FrameAssembler {                               // 单帧重组状态结构体
    uint32_t frame_id = 0;                           // 当前正在重组的帧号
    uint16_t width = 0;                              // 当前帧宽度
    uint16_t height = 0;                             // 当前帧高度
    uint8_t pixel_format = PIX_FMT_RGB24;            // 当前帧像素格式
    uint16_t frag_cnt = 0;                           // 当前帧总分片数
    std::vector<uint8_t> frame_data;                 // 完整帧缓冲区（按分片拼接空间）
    std::vector<uint8_t> frag_recv;                  // 分片接收标记，1 表示已收到
    std::vector<uint16_t> frag_sizes;                // 每个分片的有效载荷长度
    uint16_t recv_cnt = 0;                           // 已收到分片数量
    std::chrono::steady_clock::time_point start_tp;  // 首片到达时间，用于超时丢帧
};

constexpr std::size_t MAX_INFLIGHT_FRAMES = 32;      // 并行重组帧上限，防止异常情况下内存膨胀

void reset_assembler(FrameAssembler* as) {           // 重置重组状态
    as->frame_id = 0;                                // 清空帧号
    as->width = 0;                                   // 清空宽度
    as->height = 0;                                  // 清空高度
    as->pixel_format = PIX_FMT_RGB24;                // 清空像素格式
    as->frag_cnt = 0;                                // 清空分片总数
    as->frame_data.clear();                          // 清空帧缓存
    as->frag_recv.clear();                           // 清空分片标记
    as->frag_sizes.clear();                          // 清空分片长度记录
    as->recv_cnt = 0;                                // 清空已收分片计数
}

bool init_new_frame(FrameAssembler* as, const VideoPacketHeader& hdr) { // 根据新帧包头初始化重组状态
    if (hdr.width == 0 || hdr.height == 0 || hdr.frag_cnt == 0) { // 基础字段合法性检查
        return false;                                 // 参数非法则初始化失败
    }

    const std::size_t frame_bytes = static_cast<std::size_t>(hdr.frag_cnt) * UDP_SAFE_PAYLOAD; // 按分片上限分配拼接空间
    as->frame_id = hdr.frame_id;                      // 记录当前帧号
    as->width = hdr.width;                            // 记录当前宽度
    as->height = hdr.height;                          // 记录当前高度
    as->pixel_format = hdr.pixel_format;              // 记录当前像素格式
    as->frag_cnt = hdr.frag_cnt;                      // 记录总分片数
    as->frame_data.assign(frame_bytes, 0);            // 分配完整帧缓存
    as->frag_recv.assign(hdr.frag_cnt, 0);            // 分配分片接收标记
    as->frag_sizes.assign(hdr.frag_cnt, 0);           // 分配分片长度记录
    as->recv_cnt = 0;                                 // 已收分片计数清零
    as->start_tp = std::chrono::steady_clock::now();  // 记录帧开始接收时间
    return true;                                      // 初始化成功
}

std::FILE* start_ffmpeg_pipe(uint16_t width, uint16_t height, uint8_t pixel_format) { // 启动 ffmpeg 子进程并返回 stdin 管道
    std::ostringstream cmd;                           // 组织 ffmpeg 命令
    if (pixel_format == PIX_FMT_MJPEG) {              // MJPEG 输入分支
        cmd << "ffmpeg -y -f image2pipe -vcodec mjpeg -framerate " << VIDEO_FPS
            << " -i - -an -c:v mpeg4 -q:v 5 output.mp4"; // 使用 image2pipe 读取逐帧 JPEG 并编码为 mp4
    } else {                                           // RGB24 输入分支
        cmd << "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size "
            << width << "x" << height
            << " -framerate " << VIDEO_FPS
            << " -i - -an -c:v mpeg4 -q:v 5 output.mp4"; // 使用 mpeg4 避免依赖 libx264
    }

    std::cout << "[PC] start ffmpeg: " << cmd.str() << std::endl; // 打印 ffmpeg 启动命令
    std::FILE* pipe = popen(cmd.str().c_str(), "w");               // 以写入方式打开子进程标准输入
    if (!pipe) {                                                     // 判断是否启动失败
        std::cerr << "[PC] popen ffmpeg failed" << std::endl;      // 打印失败日志
    }
    return pipe;                                                     // 返回管道句柄
}

bool find_jpeg_span(const std::vector<uint8_t>& data, std::size_t size, std::size_t* start, std::size_t* length) { // 在组帧数据内定位 JPEG 有效范围
    if (!start || !length || size < 4 || data.size() < size) { // 基础参数合法性检查
        return false;                                  // 参数非法
    }

    std::size_t soi = size;                            // 记录 SOI 起始偏移
    for (std::size_t i = 0; i + 1 < size; ++i) {      // 从头扫描 JPEG 起始标记
        if (data[i] == 0xFF && data[i + 1] == 0xD8) { // 匹配 SOI (FFD8)
            soi = i;                                   // 保存 SOI 位置
            break;                                     // 找到首个 SOI 即可
        }
    }
    if (soi == size) {                                 // 未找到 SOI 分支
        return false;                                  // 判定为无效 JPEG
    }

    std::size_t eoi = size;                            // 记录 EOI 结束偏移（含 2 字节）
    for (std::size_t i = size - 1; i > soi; --i) {    // 从尾部反向扫描 JPEG 结束标记
        if (data[i - 1] == 0xFF && data[i] == 0xD9) { // 匹配 EOI (FFD9)
            eoi = i + 1;                               // 转成半开区间结束偏移
            break;                                     // 找到最后一个 EOI 即可
        }
    }
    if (eoi == size || eoi <= soi + 2) {              // 未找到 EOI 或长度非法
        return false;                                  // 判定为无效 JPEG
    }

    *start = soi;                                      // 输出 JPEG 起始偏移
    *length = eoi - soi;                               // 输出 JPEG 有效长度
    return true;                                       // 成功定位 JPEG 边界
}
} // namespace

int main() {                                         // 主函数入口
    std::signal(SIGINT, handle_signal);              // 注册 Ctrl+C 信号处理
    std::signal(SIGTERM, handle_signal);             // 注册终止信号处理

    int fd_pc = create_socket();                     // 创建 PC 侧 UDP socket
    if (fd_pc < 0) {                                 // 判断创建是否失败
        return 1;                                    // 返回错误码
    }                                                // 结束失败判断

    if (!bind_ip_port(fd_pc, PC_IP, PC_PORT)) {     // 绑定 PC 本机 IP 与监听端口
        close(fd_pc);                                // 关闭 socket
        return 1;                                    // 返回错误码
    }                                                // 结束绑定判断
    tune_socket_buffers(fd_pc, DEFAULT_UDP_RCVBUF_BYTES, DEFAULT_UDP_SNDBUF_BYTES); // 增大 UDP 缓冲区，降低分片丢失概率

    timeval timeout{};                               // 定义接收超时配置
    timeout.tv_sec = 0;                              // 秒级超时设置为 0
    timeout.tv_usec = 20000;                         // 微秒级超时 20ms，便于周期检查退出与丢帧
    setsockopt(fd_pc, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); // 设置接收超时

    std::cout << "[PC] wait video packets and save to output.mp4" << std::endl; // 打印启动信息

    std::unordered_map<uint32_t, FrameAssembler> inflight; // 创建并行重组容器，键为 frame_id
    std::FILE* ffmpeg_pipe = nullptr;                // ffmpeg 写入管道句柄
    uint8_t ffmpeg_input_format = 0;                 // ffmpeg 当前输入格式标记
    uint64_t ok_frame_cnt = 0;                       // 成功写入帧计数
    uint64_t drop_frame_cnt = 0;                     // 超时或异常丢帧计数

    while (!g_stop) {                                // 持续接收直到收到停止信号
        std::vector<uint8_t> packet;                 // 定义接收包缓冲区
        sockaddr_in src{};                           // 定义源地址
        socklen_t srclen = sizeof(src);              // 定义源地址长度
        int n = recv_bytes(fd_pc, &packet, &src, &srclen); // 接收 310B 转发包
        if (n <= 0) {                                // 超时或无数据分支
            if (!inflight.empty()) {                 // 仅在有在途帧时做超时检查
                auto now = std::chrono::steady_clock::now(); // 获取当前时间
                std::vector<uint32_t> timeout_ids;   // 收集超时帧 id，避免遍历中删除
                for (const auto& kv : inflight) {    // 遍历所有在途帧
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - kv.second.start_tp).count(); // 计算该帧已等待时间
                    if (elapsed_ms > FRAME_TIMEOUT_MS) { // 判断是否超时
                        timeout_ids.push_back(kv.first); // 记录超时帧 id
                    }
                }
                for (uint32_t id : timeout_ids) {    // 删除所有超时帧
                    ++drop_frame_cnt;                // 统计丢帧
                    std::cout << "[PC] drop frame timeout frame_id=" << id << std::endl; // 打印超时日志
                    inflight.erase(id);              // 从在途容器移除该帧
                }
            }
            continue;                                // 继续下一轮接收
        }

        VideoPacketHeader hdr{};                     // 定义解析后的视频包头
        if (!parse_video_header(packet, &hdr)) {     // 尝试按视频包格式解析
            std::cout << "[PC] ignore non-video packet size=" << packet.size() << std::endl; // 非视频包直接忽略
            continue;                                // 继续接收下一包
        }

        auto it = inflight.find(hdr.frame_id);       // 查找当前帧是否已在途
        if (it == inflight.end()) {                  // 新帧首次出现
            if (inflight.size() >= MAX_INFLIGHT_FRAMES) { // 在途帧过多时主动淘汰最老帧
                uint32_t oldest_id = hdr.frame_id;   // 初始化最老帧号
                auto oldest_tp = std::chrono::steady_clock::now(); // 初始化最早时间戳
                bool has_oldest = false;             // 标记是否找到候选最老帧
                for (const auto& kv : inflight) {    // 遍历在途帧选择最老项
                    if (!has_oldest || kv.second.start_tp < oldest_tp) { // 比较到达时间
                        has_oldest = true;           // 标记已找到
                        oldest_id = kv.first;        // 更新最老帧号
                        oldest_tp = kv.second.start_tp; // 更新最早时间
                    }
                }
                if (has_oldest) {                    // 成功找到淘汰目标
                    ++drop_frame_cnt;                // 统计丢帧
                    std::cout << "[PC] drop overflow frame frame_id=" << oldest_id << std::endl; // 打印淘汰日志
                    inflight.erase(oldest_id);       // 删除最老在途帧
                }
            }

            FrameAssembler fresh;                    // 创建新帧重组状态
            if (!init_new_frame(&fresh, hdr)) {      // 初始化新帧重组器
                std::cout << "[PC] invalid frame init frame_id=" << hdr.frame_id << std::endl; // 打印初始化失败日志
                continue;                            // 跳过该包
            }

            auto insert_ret = inflight.emplace(hdr.frame_id, std::move(fresh)); // 插入新在途帧
            it = insert_ret.first;                   // 更新迭代器到当前帧
        }

        FrameAssembler& assembler = it->second;      // 获取当前帧重组状态引用

        if (hdr.frag_cnt != assembler.frag_cnt || hdr.width != assembler.width || hdr.height != assembler.height || hdr.pixel_format != assembler.pixel_format) { // 同帧关键参数一致性检查
            ++drop_frame_cnt;                        // 参数不一致按损坏帧处理
            std::cout << "[PC] drop frame param mismatch frame_id=" << hdr.frame_id << std::endl; // 打印参数不一致日志
            inflight.erase(hdr.frame_id);            // 删除异常在途帧
            continue;                                // 等待下一帧
        }

        if (hdr.frag_idx >= assembler.frag_cnt) {    // 分片序号越界检查
            std::cout << "[PC] ignore invalid frag idx=" << hdr.frag_idx << std::endl; // 打印越界日志
            continue;                                // 跳过异常分片
        }

        if (hdr.payload_bytes > UDP_SAFE_PAYLOAD) {  // 单片载荷上限检查
            std::cout << "[PC] ignore too large payload=" << hdr.payload_bytes << std::endl; // 打印异常载荷日志
            continue;                                // 跳过异常分片
        }

        const std::size_t payload_offset = sizeof(VideoPacketHeader); // 定位分片载荷起始偏移
        const std::size_t target_offset = static_cast<std::size_t>(hdr.frag_idx) * UDP_SAFE_PAYLOAD; // 计算写入帧缓存偏移
        if (target_offset + hdr.payload_bytes > assembler.frame_data.size()) { // 写入范围越界检查
            ++drop_frame_cnt;                        // 越界按损坏帧处理
            std::cout << "[PC] drop frame out-of-range frame_id=" << hdr.frame_id << std::endl; // 打印越界日志
            inflight.erase(hdr.frame_id);            // 删除异常在途帧
            continue;                                // 等待下一帧
        }

        if (!assembler.frag_recv[hdr.frag_idx]) {    // 仅在该分片首次到达时写入
            std::memcpy(assembler.frame_data.data() + target_offset, packet.data() + payload_offset, hdr.payload_bytes); // 拷贝分片数据到帧缓冲
            assembler.frag_recv[hdr.frag_idx] = 1;   // 标记该分片已收到
            assembler.frag_sizes[hdr.frag_idx] = hdr.payload_bytes; // 记录该分片有效负载长度
            ++assembler.recv_cnt;                    // 已收分片计数递增
        }

        if (assembler.recv_cnt == assembler.frag_cnt) { // 当前帧分片全部收齐
            std::size_t frame_bytes = 0;             // 定义完整帧实际字节数
            for (uint16_t i = 0; i < assembler.frag_cnt; ++i) { // 累加每片有效长度
                frame_bytes += assembler.frag_sizes[i]; // 计算真实帧字节数
            }

            if (assembler.pixel_format == PIX_FMT_RGB24) { // RGB24 输入时做长度一致性校验
                const std::size_t expected = static_cast<std::size_t>(assembler.width) * static_cast<std::size_t>(assembler.height) * 3; // 理论 RGB24 帧长度
                if (frame_bytes != expected) {       // 长度不一致时丢帧
                    ++drop_frame_cnt;                // 统计丢帧
                    std::cout << "[PC] drop RGB24 frame size mismatch bytes=" << frame_bytes
                              << " expected=" << expected << std::endl; // 打印长度不一致日志
                    inflight.erase(hdr.frame_id);    // 删除该异常在途帧
                    continue;                        // 跳过该帧写入
                }
            }

            std::size_t write_offset = 0;            // 定义写入偏移，默认从 0 开始
            std::size_t write_size = frame_bytes;    // 定义写入长度，默认等于完整帧长度
            if (assembler.pixel_format == PIX_FMT_MJPEG) { // MJPEG 输入时定位有效 JPEG 边界
                if (!find_jpeg_span(assembler.frame_data, frame_bytes, &write_offset, &write_size)) { // 查找 SOI/EOI
                    ++drop_frame_cnt;                // 统计丢帧
                    std::cout << "[PC] drop invalid JPEG frame frame_id=" << hdr.frame_id << " bytes=" << frame_bytes << std::endl; // 打印无效 JPEG 日志
                    inflight.erase(hdr.frame_id);    // 删除该异常在途帧
                    continue;                        // 跳过该帧写入
                }
            }

            if (!ffmpeg_pipe) {                      // 在第一帧完整有效时再启动 ffmpeg
                ffmpeg_pipe = start_ffmpeg_pipe(assembler.width, assembler.height, assembler.pixel_format); // 按首个完整帧启动编码器
                if (!ffmpeg_pipe) {                  // 启动失败时终止程序
                    close(fd_pc);                    // 关闭 socket
                    return 1;                        // 返回错误码
                }
                ffmpeg_input_format = assembler.pixel_format; // 记录编码器输入格式
            }

            if (assembler.pixel_format != ffmpeg_input_format) { // 禁止运行中输入格式突变
                ++drop_frame_cnt;                    // 统计丢帧
                std::cout << "[PC] drop format switch frame_id=" << hdr.frame_id << std::endl; // 打印格式切换日志
                inflight.erase(hdr.frame_id);        // 删除该在途帧
                continue;                            // 等待后续稳定帧
            }

            std::size_t written = std::fwrite(assembler.frame_data.data() + write_offset, 1, write_size, ffmpeg_pipe); // 写入 ffmpeg 管道
            if (written != write_size) {             // 判断写入是否完整
                std::cerr << "[PC] write ffmpeg pipe failed" << std::endl; // 打印失败日志
                break;                               // 中断主循环，准备收尾退出
            }
            std::fflush(ffmpeg_pipe);                // 立即刷新，降低编码缓存延迟

            ++ok_frame_cnt;                          // 成功写帧计数递增
            if (ok_frame_cnt % 30 == 0) {            // 每 30 帧打印一次状态
                std::cout << "[PC] write frame ok=" << ok_frame_cnt
                          << " drop=" << drop_frame_cnt
                          << " inflight=" << inflight.size() << std::endl; // 打印当前统计
            }

            inflight.erase(hdr.frame_id);            // 完成写入后删除该在途帧
        }
    }                                                // 结束循环

    if (ffmpeg_pipe) {                               // 若 ffmpeg 管道已启动
        std::fflush(ffmpeg_pipe);                    // 退出前再刷一次缓存
        int rc = pclose(ffmpeg_pipe);                // 关闭管道并等待 ffmpeg 正常收尾
        std::cout << "[PC] ffmpeg exit code=" << rc << std::endl; // 打印 ffmpeg 退出状态
    }

    std::cout << "[PC] stop, ok_frame=" << ok_frame_cnt << " drop_frame=" << drop_frame_cnt << std::endl; // 打印最终统计

    close(fd_pc);                                    // 关闭 socket
    return 0;                                        // 返回成功码
}                                                    // 主函数结束