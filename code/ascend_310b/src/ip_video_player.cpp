#include "ip_video_player.h"
#include "udp_common.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace ascend310b {

// 使用 udp_common.h 中定义的宏
using namespace udp_net;

IpVideoPlayer::IpVideoPlayer(std::string pc_ip, int video_port)
    : pc_ip_(std::move(pc_ip))
    , video_port_(video_port) {
}

IpVideoPlayer::~IpVideoPlayer() {
    shutdown();
}

bool IpVideoPlayer::init() {
    if (udp_socket_ >= 0) {
        return true; // 已初始化
    }
    
    udp_socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket_ < 0) {
        std::cerr << "[IpVideoPlayer] socket create failed: " << std::strerror(errno) << std::endl;
        return false;
    }
    
    // 设置 socket 缓冲区，使用 udp_common.h 中定义的默认值
    (void)::setsockopt(udp_socket_, SOL_SOCKET, SO_RCVBUF, 
                       &DEFAULT_UDP_RCVBUF_BYTES, sizeof(DEFAULT_UDP_RCVBUF_BYTES));
    (void)::setsockopt(udp_socket_, SOL_SOCKET, SO_SNDBUF, 
                       &DEFAULT_UDP_SNDBUF_BYTES, sizeof(DEFAULT_UDP_SNDBUF_BYTES));
    
    running_.store(true, std::memory_order_relaxed);
    control_thread_ = std::thread(&IpVideoPlayer::controlLoop, this);
    
    std::cout << "[IpVideoPlayer] initialized, PC=" << pc_ip_ << ":" << video_port_ << std::endl;
    return true;
}

bool IpVideoPlayer::registerVideo(int action_type, const std::string& video_path, int priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    VideoAsset asset;
    asset.path = video_path;
    asset.priority = priority;
    asset.action_type = action_type;
    
    auto& assets = video_assets_[action_type];
    assets.push_back(asset);
    
    // 按优先级排序
    std::sort(assets.begin(), assets.end(), [](const VideoAsset& a, const VideoAsset& b) {
        return a.priority < b.priority;
    });
    
    std::cout << "[IpVideoPlayer] registered video: action=" << action_type 
              << " path=" << video_path << " priority=" << priority << std::endl;
    return true;
}

bool IpVideoPlayer::triggerAction(int action_type, float confidence, uint32_t frame_id) {
    if (!running_.load(std::memory_order_relaxed)) {
        return false;
    }
    
    // 检查是否有对应的视频资源
    const VideoAsset* asset = findBestVideo(action_type);
    if (!asset) {
        std::cerr << "[IpVideoPlayer] no video registered for action=" << action_type << std::endl;
        return false;
    }
    
    // 如果当前正在播放且不允许打断，则忽略新请求
    if (playing_.load(std::memory_order_relaxed) && !allow_interrupt_.load(std::memory_order_relaxed)) {
        return false;
    }
    
    PlayRequest request;
    request.action_type = action_type;
    request.confidence = confidence;
    request.frame_id = frame_id;
    request.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 如果队列已满，移除最旧的请求
        while (play_queue_.size() >= kMaxVideoQueueSize) {
            play_queue_.pop_front();
        }
        
        play_queue_.push_back(request);
    }
    
    cv_.notify_one();
    return true;
}

void IpVideoPlayer::stop() {
    sendStopCommand();
    playing_.store(false, std::memory_order_relaxed);
    paused_.store(false, std::memory_order_relaxed);
    
    std::lock_guard<std::mutex> lock(mutex_);
    current_video_.clear();
    play_queue_.clear();
}

void IpVideoPlayer::pause() {
    if (playing_.load(std::memory_order_relaxed)) {
        sendPauseCommand();
        paused_.store(true, std::memory_order_relaxed);
    }
}

void IpVideoPlayer::resume() {
    if (paused_.load(std::memory_order_relaxed)) {
        sendResumeCommand();
        paused_.store(false, std::memory_order_relaxed);
    }
}

void IpVideoPlayer::setAllowInterrupt(bool allow) {
    allow_interrupt_.store(allow, std::memory_order_relaxed);
}

bool IpVideoPlayer::isPlaying() const {
    return playing_.load(std::memory_order_relaxed);
}

std::string IpVideoPlayer::getCurrentVideo() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_video_;
}

void IpVideoPlayer::shutdown() {
    running_.store(false, std::memory_order_relaxed);
    cv_.notify_all();
    
    if (control_thread_.joinable()) {
        control_thread_.join();
    }
    
    stop();
    
    if (udp_socket_ >= 0) {
        ::close(udp_socket_);
        udp_socket_ = -1;
    }
    
    std::cout << "[IpVideoPlayer] shutdown complete" << std::endl;
}

void IpVideoPlayer::controlLoop() {
    std::cout << "[IpVideoPlayer] control loop started" << std::endl;
    
    while (running_.load(std::memory_order_relaxed)) {
        PlayRequest request;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !running_.load(std::memory_order_relaxed) || !play_queue_.empty();
            });
            
            if (!running_.load(std::memory_order_relaxed)) {
                break;
            }
            
            if (play_queue_.empty()) {
                continue;
            }
            
            request = play_queue_.front();
            play_queue_.pop_front();
        }
        
        // 查找最佳匹配视频
        const VideoAsset* asset = findBestVideo(request.action_type);
        if (!asset) {
            continue;
        }
        
        // 发送播放指令
        if (sendPlayCommand(asset->path)) {
            std::lock_guard<std::mutex> lock(mutex_);
            current_video_ = asset->path;
            playing_.store(true, std::memory_order_relaxed);
            paused_.store(false, std::memory_order_relaxed);
            
            std::cout << "[IpVideoPlayer] playing: " << asset->path 
                      << " action=" << request.action_type
                      << " conf=" << std::fixed << std::setprecision(3) << request.confidence
                      << " frame_id=" << request.frame_id << std::endl;
        }
    }
    
    std::cout << "[IpVideoPlayer] control loop exited" << std::endl;
}

bool IpVideoPlayer::sendPlayCommand(const std::string& video_path) {
    if (udp_socket_ < 0) {
        std::cerr << "[IpVideoPlayer] socket not initialized" << std::endl;
        return false;
    }
    
    // 使用 udp_common.h 中的 send_video_ctrl_cmd 函数
    std::string json = buildPlayJson(video_path, 0);
    sockaddr_in addr = make_addr(pc_ip_, video_port_);
    if (addr.sin_family == 0) {
        std::cerr << "[IpVideoPlayer] invalid PC IP: " << pc_ip_ << std::endl;
        return false;
    }
    
    int sent = send_video_ctrl_cmd(udp_socket_, json, addr);
    return sent == static_cast<int>(json.size());
}

bool IpVideoPlayer::sendStopCommand() {
    if (udp_socket_ < 0) {
        return false;
    }
    
    std::string json = R"({"cmd":"stop"})";
    sockaddr_in addr = make_addr(pc_ip_, video_port_);
    if (addr.sin_family == 0) {
        return false;
    }
    
    int sent = send_video_ctrl_cmd(udp_socket_, json, addr);
    return sent == static_cast<int>(json.size());
}

bool IpVideoPlayer::sendPauseCommand() {
    if (udp_socket_ < 0) {
        return false;
    }
    
    std::string json = R"({"cmd":"pause"})";
    sockaddr_in addr = make_addr(pc_ip_, video_port_);
    if (addr.sin_family == 0) {
        return false;
    }
    
    int sent = send_video_ctrl_cmd(udp_socket_, json, addr);
    return sent == static_cast<int>(json.size());
}

bool IpVideoPlayer::sendResumeCommand() {
    if (udp_socket_ < 0) {
        return false;
    }
    
    std::string json = R"({"cmd":"resume"})";
    sockaddr_in addr = make_addr(pc_ip_, video_port_);
    if (addr.sin_family == 0) {
        return false;
    }
    
    int sent = send_video_ctrl_cmd(udp_socket_, json, addr);
    return sent == static_cast<int>(json.size());
}

const IpVideoPlayer::VideoAsset* IpVideoPlayer::findBestVideo(int action_type) const {
    auto it = video_assets_.find(action_type);
    if (it == video_assets_.end() || it->second.empty()) {
        return nullptr;
    }
    
    // 返回优先级最高的视频（已排序）
    return &it->second[0];
}

std::string IpVideoPlayer::buildPlayJson(const std::string& video_path, uint32_t frame_id) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0);
    oss << "{\"cmd\":\"play\",\"video\":\"" << video_path << "\",\"frame_id\":" << frame_id << "}";
    return oss.str();
}

} // namespace ascend310b
