#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ascend310b {

/**
 * @brief IP 视频联动播放控制模块
 * 
 * 功能：
 * 1. 管理预生成的视频资产（wave1.mp4, wave2.mp4, heart1.mp4 等）
 * 2. 根据动作/表情识别结果触发对应视频播放
 * 3. 支持无缝切换播放控制
 * 4. 通过 UDP 向 PC 端发送播放控制指令
 */
class IpVideoPlayer {
public:
    // 动作类型定义，与 main.cpp 中的动作标识保持一致
    static constexpr int kActionUnknown = 0;
    static constexpr int kActionWave = 1;
    static constexpr int kActionCheer = 2;
    
    // 默认配置
    static constexpr int kDefaultVideoPort = 5567;  // 视频控制 UDP 端口
    static constexpr int kMaxVideoQueueSize = 8;    // 最大待播放视频队列长度
    
    /**
     * @brief 构造函数
     * @param pc_ip PC 端 IP 地址
     * @param video_port 视频控制 UDP 端口
     */
    explicit IpVideoPlayer(std::string pc_ip = "192.168.137.1", 
                          int video_port = kDefaultVideoPort);
    
    ~IpVideoPlayer();
    
    /**
     * @brief 初始化播放器（建立 UDP 连接等）
     * @return 成功返回 true，失败返回 false
     */
    bool init();
    
    /**
     * @brief 注册视频资产
     * @param action_type 动作类型 (kActionWave, kActionCheer 等)
     * @param video_path 视频文件路径
     * @param priority 优先级（数字越小优先级越高）
     * @return 成功返回 true
     */
    bool registerVideo(int action_type, const std::string& video_path, int priority = 0);
    
    /**
     * @brief 触发动作对应的视频播放
     * @param action_type 动作类型
     * @param confidence 动作置信度
     * @param frame_id 帧 ID（用于追踪）
     * @return 成功返回 true
     */
    bool triggerAction(int action_type, float confidence, uint32_t frame_id);
    
    /**
     * @brief 立即停止当前播放
     */
    void stop();
    
    /**
     * @brief 暂停播放
     */
    void pause();
    
    /**
     * @brief 恢复播放
     */
    void resume();
    
    /**
     * @brief 设置是否允许打断当前播放
     * @param allow 是否允许打断
     */
    void setAllowInterrupt(bool allow);
    
    /**
     * @brief 获取当前播放状态
     * @return true 表示正在播放
     */
    bool isPlaying() const;
    
    /**
     * @brief 获取当前播放的视频路径
     * @return 视频路径字符串
     */
    std::string getCurrentVideo() const;
    
    /**
     * @brief 关闭播放器并释放资源
     */
    void shutdown();

private:
    struct VideoAsset {
        std::string path;
        int priority;
        int action_type;
    };
    
    struct PlayRequest {
        int action_type;
        float confidence;
        uint32_t frame_id;
        uint64_t timestamp_ms;
    };
    
    // 内部方法
    void controlLoop();
    bool sendPlayCommand(const std::string& video_path);
    bool sendStopCommand();
    bool sendPauseCommand();
    bool sendResumeCommand();
    const VideoAsset* findBestVideo(int action_type) const;
    std::string buildPlayJson(const std::string& video_path, uint32_t frame_id) const;
    
    // 成员变量
    std::string pc_ip_;
    int video_port_;
    int udp_socket_{-1};
    
    std::atomic<bool> running_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> allow_interrupt_{true};
    
    std::string current_video_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    
    // 视频资产管理：action_type -> [VideoAsset, ...]
    std::unordered_map<int, std::vector<VideoAsset>> video_assets_;
    
    // 播放请求队列
    std::deque<PlayRequest> play_queue_;
    
    // 控制线程
    std::thread control_thread_;
};

} // namespace ascend310b
