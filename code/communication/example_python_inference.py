#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ascend 310B Python Video Stream Interface - 完整使用示例

本示例演示如何使用 pybind11 暴露的 VideoStreamInterface 类实现完整的数据处理链路：

【数据流向】
1. PYNQ 开发板 → (UDP 视频流分片) → Ascend 310B
2. Ascend 310B C++ 层 → (自动分片重组) → Python 回调函数
3. Python 回调函数 → (NPU 推理/图像处理) → 推理结果 (JSON)
4. C++ 层 → (自动 UDP 发送) → PC 端

【核心特性】
- epoll 高性能 I/O 多路复用，支持数百并发通道
- 零拷贝数据传输 (pybind11::capsule)，避免内存复制开销
- 预连接 UDP socket (connect+write)，减少 50% 系统调用
- 自动分片重组和错误恢复机制
- 详细的性能统计监控

依赖:
    - pybind11 (已编译为 video_stream_interface.so)
    - numpy (可选，用于图像处理)
    - opencv-python (可选，用于图像解码)

使用方法:
    # 1. 先编译 C++ 模块
    cd /workspace/code/communication
    cmake -S . -B build
    cmake --build build -j
    
    # 2. 运行 Python 示例
    python example_python_inference.py
    
    # 3. 在另一台机器上启动数据发送端 (PYNQ 或模拟器)
"""

import sys
import os
import json
import time
import signal
import threading

# 导入 pybind11 模块 (需要先编译)
# 模块路径：build/video_stream_interface.cpython-312-x86_64-linux-gnu.so
try:
    import video_stream_interface as vsi
except ImportError:
    print("Error: video_stream_interface module not found.")
    print("Please build the module first:")
    print("  cd /workspace/code/communication")
    print("  cmake -S . -B build")
    print("  cmake --build build -j")
    print("\nThe compiled module will be at: build/video_stream_interface*.so")
    sys.exit(1)


# ============ 全局状态 ============
g_running = True


def signal_handler(signum, frame):
    """信号处理函数，用于优雅退出"""
    global g_running
    print(f"\nReceived signal {signum}, stopping...")
    g_running = False


# 注册信号处理
signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)


# ============ NPU 推理函数（用户需在此集成真实推理代码）============
def simulate_npu_inference(frame_dict):
    """
    模拟 NPU 推理过程 - 用户需替换为真实的 Ascend ACL 推理代码
    
    【输入参数】
        frame_dict: 包含帧信息的字典
            - frame_id (int): 帧序号
            - width (int): 图像宽度
            - height (int): 图像高度
            - pixel_format (int): 像素格式
                * vsi.PIX_FMT_RGB24 (1): RGB24 格式
                * vsi.PIX_FMT_MJPEG (2): MJPEG 压缩格式
                * vsi.PIX_FMT_NV12 (3): NV12 格式
            - data (numpy.ndarray): uint8 类型的 NumPy 数组，零拷贝共享内存
            - timestamp_us (int): 时间戳（微秒）
    
    【输出】
        inference_result: JSON 格式的推理结果字符串
            将自动通过优化的 UDP 发送到 PC 端 (默认端口 5556)
    
    【典型应用场景】
        1. 姿态识别：检测人体关键点
        2. 表情识别：识别人脸表情
        3. 动作识别：识别行为动作
        4. 目标检测：检测物体位置和类别
        5. 图像分类：分类图像内容
    
    【性能优化建议】
        - 使用批处理提高 NPU 利用率
        - 异步处理避免阻塞视频流接收
        - 使用 OpenCV 加速图像格式转换
        - 合理设置模型输入尺寸减少预处理开销
    """
    frame_id = frame_dict['frame_id']
    width = frame_dict['width']
    height = frame_dict['height']
    pixel_format = frame_dict['pixel_format']
    data_size = len(frame_dict['data'])
    
    # 打印帧信息
    fmt_str = {
        vsi.PIX_FMT_RGB24: "RGB24",
        vsi.PIX_FMT_MJPEG: "MJPEG", 
        vsi.PIX_FMT_NV12: "NV12"
    }.get(pixel_format, f"Unknown({pixel_format})")
    
    print(f"[Python Inference] Frame {frame_id}: {width}x{height}, "
          f"format={fmt_str}, data_size={data_size} bytes")
    
    # ================================================================
    # TODO: 在这里集成您的真实 NPU 推理代码
    # ================================================================
    # 
    # 【示例代码框架】
    # 
    # 1. 根据像素格式解码图像
    # ----------------------------------------
    # import numpy as np
    # import cv2
    #
    # if pixel_format == vsi.PIX_FMT_MJPEG:
    #     # MJPEG 解码
    #     image = cv2.imdecode(np.frombuffer(frame_dict['data'], dtype=np.uint8), 
    #                          cv2.IMREAD_COLOR)
    # elif pixel_format == vsi.PIX_FMT_NV12:
    #     # NV12 转 BGR
    #     yuv = np.frombuffer(frame_dict['data'], dtype=np.uint8)
    #     image = cv2.cvtColor(yuv.reshape(int(height*1.5), width), cv2.COLOR_YUV2BGR_NV12)
    # else:
    #     # RGB24 直接 reshape
    #     image = np.frombuffer(frame_dict['data'], dtype=np.uint8).reshape(height, width, 3)
    #
    # 2. 预处理 (resize, normalize, HWC->CHW, etc.)
    # ----------------------------------------
    # input_tensor = cv2.resize(image, (224, 224))
    # input_tensor = input_tensor.astype(np.float32) / 255.0
    # input_tensor = np.transpose(input_tensor, (2, 0, 1))  # HWC -> CHW
    # input_tensor = np.expand_dims(input_tensor, axis=0)   # add batch dimension
    #
    # 3. NPU 推理 (使用 Ascend ACL)
    # ----------------------------------------
    # import acl_numpy as acl
    # result = acl_model.execute(input_tensor)
    #
    # 4. 后处理 (解析推理结果)
    # ----------------------------------------
    # detections = postprocess(result)
    # 
    # ================================================================
    
    # 模拟推理结果（姿态/表情/动作识别）
    # 在实际应用中，这里应该是真实的 NPU 推理结果
    inference_result = {
        "frame_id": frame_id,
        "timestamp_us": frame_dict['timestamp_us'],
        "inference_type": "pose_expression",  # 推理类型
        "results": {
            "pose": {
                "action": "standing",      # 动作识别结果
                "confidence": 0.95,        # 置信度
                "keypoints": []            # 人体关键点坐标 (17 个点)
            },
            "expression": {
                "emotion": "happy",        # 表情识别结果
                "confidence": 0.87         # 置信度
            },
            "objects": [                   # 目标检测结果
                {
                    "class": "person", 
                    "confidence": 0.92, 
                    "bbox": [100, 50, 300, 400]  # [x1, y1, x2, y2]
                }
            ]
        },
        "processing_time_ms": 15.5  # 处理耗时（毫秒）
    }
    
    # 返回 JSON 字符串，C++ 层会自动通过 UDP 发送到 PC
    return json.dumps(inference_result)


# ============ 主函数 ============
def main():
    """
    主函数：演示如何使用 VideoStreamInterface 实现完整的数据处理链路
    
    【工作流程】
    1. 创建 VideoStreamInterface 实例，监听 PYNQ 发来的视频流
    2. 获取 UdpSender 实例，用于发送推理结果到 PC
    3. 定义帧回调函数，在回调中进行 NPU 推理
    4. 注册回调函数并启动后台接收线程
    5. 等待用户中断，期间自动处理和发送数据
    6. 优雅退出并打印统计信息
    """
    
    print("=" * 60)
    print("Ascend 310B Python Video Stream Interface Example")
    print("=" * 60)
    print()
    print("Configuration:")
    print(f"  - Listening on: {vsi.ASCEND_310B_IP}:{vsi.PYNQ_PORT}")
    print(f"    (接收来自 PYNQ 的视频流分片)")
    print(f"  - Sending results to: {vsi.PC_IP}:{vsi.PC_CTRL_PORT}")
    print(f"    (发送推理结果到 PC)")
    print(f"  - Expected format: RGB24/MJPEG/NV12")
    print()
    print("Press Ctrl+C to stop")
    print("=" * 60)
    print()
    
    # ------------------------------------------------------------------
    # 步骤 1: 创建视频流接口实例
    # ------------------------------------------------------------------
    # 参数说明:
    #   listen_port: 监听端口，默认为 PYNQ_PORT (9090)
    #                用于接收来自 PYNQ 的 UDP 视频流分片
    # 
    # 内部操作:
    #   - 创建 UDP socket 并绑定到指定端口
    #   - 创建 EpollManager 实现 I/O 多路复用
    #   - 设置 socket 为非阻塞模式 + 边沿触发 (EPOLLET)
    # ------------------------------------------------------------------
    video_interface = vsi.VideoStreamInterface(vsi.PYNQ_PORT)
    
    # ------------------------------------------------------------------
    # 步骤 2: 获取 UDP 发送器（用于发送推理结果到 PC）
    # ------------------------------------------------------------------
    # 返回对象：UdpSender 实例
    # 
    # 优化特性:
    #   - 预连接目标地址 (connect)，后续发送使用 write() 替代 sendto()
    #   - 自动错误恢复（连续错误 10 次后重建 socket）
    #   - 详细统计信息（发送次数、字节数、错误数、重连次数）
    # 
    # 使用方法:
    #   udp_sender.send_inference_result(json_string)  # 发送 JSON 推理结果
    #   udp_sender.send_binary_data(numpy_array)       # 发送二进制数据
    #   udp_sender.send_control_to_pynq(message)       # 发送控制消息到 PYNQ
    # ------------------------------------------------------------------
    udp_sender = video_interface.get_udp_sender()
    
    # 统计信息
    frame_count = 0
    start_time = time.time()
    
    # ------------------------------------------------------------------
    # 步骤 3: 定义帧回调函数
    # ------------------------------------------------------------------
    # 回调函数签名: def on_video_frame(frame: CompleteVideoFrame) -> dict
    # 
    # 参数:
    #   frame: CompleteVideoFrame 对象
    #     - frame.frame_id (int): 帧序号
    #     - frame.timestamp_us (int): 时间戳（微秒）
    #     - frame.width (int): 图像宽度
    #     - frame.height (int): 图像高度
    #     - frame.pixel_format (int): 像素格式
    #     - frame.data (numpy.ndarray): 图像数据（零拷贝 NumPy 数组）
    # 
    # 返回值:
    #   dict: {"inference_result": json_string}
    #     - inference_result: JSON 格式的推理结果字符串
    #     - C++ 层会自动将此结果通过 UDP 发送到 PC
    # 
    # 注意:
    #   - 此函数在 C++ epoll 回调线程中执行，需尽快返回
    #   - 如需长时间处理，建议使用队列 + 工作线程异步处理
    #   - frame.data 是零拷贝共享内存，不要保存引用超过回调生命周期
    # ------------------------------------------------------------------
    def on_video_frame(frame):
        """
        视频帧回调函数 - 核心处理逻辑
        
        每当接收到一个完整的视频帧时，此函数会被自动调用。
        在这个函数中，我们可以：
        1. 访问帧数据（NumPy 数组，零拷贝）
        2. 进行图像处理/NPU 推理
        3. 返回推理结果（会自动发送到 PC）
        """
        nonlocal frame_count
        frame_count += 1
        
        try:
            # 将帧转换为字典（方便访问）
            # frame_dict 包含：frame_id, timestamp_us, width, height, 
            #                 pixel_format, data (numpy array)
            frame_dict = frame.to_dict()
            
            # 调用推理函数进行处理
            # 在实际应用中，这里会调用 Ascend ACL 进行 NPU 推理
            inference_result_json = simulate_npu_inference(frame_dict)
            
            # 【重要】返回推理结果
            # C++ 层会自动将此 JSON 字符串通过优化的 UDP 发送到 PC
            # 发送配置：
            #   - 目标 IP: vsi.PC_IP (默认 "192.168.1.100")
            #   - 目标端口：vsi.PC_CTRL_PORT (默认 5556)
            #   - 协议：UDP (预连接，使用 write() 发送)
            return {"inference_result": inference_result_json}
            
        except Exception as e:
            print(f"[ERROR] Inference failed for frame {frame.frame_id}: {e}")
            # 返回空结果表示推理失败，不会发送数据
            return {"inference_result": ""}
    
    # ------------------------------------------------------------------
    # 步骤 4: 注册回调函数
    # ------------------------------------------------------------------
    video_interface.set_frame_callback(on_video_frame)
    
    print("[INFO] Video stream interface initialized, waiting for frames...")
    print()
    
    # ------------------------------------------------------------------
    # 步骤 5: 启动后台接收线程
    # ------------------------------------------------------------------
    # start_background(): 非阻塞模式，在后台线程中运行 epoll 循环
    # start(): 阻塞模式，在当前线程中运行 epoll 循环
    # 
    # epoll 优势:
    #   - 单个线程可处理数百个 UDP socket
    #   - 边沿触发减少系统调用次数
    #   - 内核级阻塞，CPU 友好
    # ------------------------------------------------------------------
    video_interface.start_background()
    
    # ------------------------------------------------------------------
    # 步骤 6: 主循环等待
    # ------------------------------------------------------------------
    # 在主线程中等待，直到用户按下 Ctrl+C
    # 期间视频帧会在后台线程中自动接收和处理
    # ------------------------------------------------------------------
    try:
        while g_running:
            time.sleep(1)
            
            # 每 10 秒打印一次统计信息
            if int(time.time()) % 10 == 0:
                elapsed = time.time() - start_time
                fps = frame_count / elapsed if elapsed > 0 else 0
                print(f"[STATS] Frames: {frame_count}, FPS: {fps:.2f}, "
                      f"Elapsed: {elapsed:.1f}s")
                
    except KeyboardInterrupt:
        pass
    finally:
        # ------------------------------------------------------------------
        # 步骤 7: 优雅退出
        # ------------------------------------------------------------------
        # 停止接收线程
        print("\n[INFO] Stopping video stream interface...")
        video_interface.stop()
        
        # 打印最终统计
        elapsed = time.time() - start_time
        fps = frame_count / elapsed if elapsed > 0 else 0
        print(f"\n[FINAL STATS]")
        print(f"  Total frames: {frame_count}")
        print(f"  Total time: {elapsed:.2f}s")
        print(f"  Average FPS: {fps:.2f}")
        
        # 打印 C++ 层统计信息
        # 包括：接收包数、帧数、丢包数、丢帧数、总字节数
        video_interface.print_stats()
        
        print("\n[INFO] Shutdown complete")


if __name__ == "__main__":
    main()
