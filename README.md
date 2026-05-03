# 集创赛 华强 x 昇腾赛题

![C++](https://img.shields.io/badge/C++-68.7%25-blue) ![VHDL](https://img.shields.io/badge/VHDL-15.1%25-orange) ![Verilog](https://img.shields.io/badge/Verilog-9.7%25-green) ![Python](https://img.shields.io/badge/Python-2.3%25-yellow)

## 项目目标

基于 PYNQ-Z2 (FPGA) + Ascend 310B (NPU) 构建异构视觉交互终端，面向园区/展演场景实现：

- **动作识别**（挥手/比心/欢呼）✅
- **表情捕捉与识别**（高兴/惊讶/生气/恐惧/悲伤/中性）✅
- **多目标管理与交互决策** ✅
- **IP 联动演艺**（熊二卡通形象视频触发）✅
- **前后端联动控制**（UDP 视频流 + WebSocket 事件）✅
- **角色语音对话**（ASR + LLM + TTS 全流程）🔄

当前阶段目标：**已完成姿态识别、表情识别、IP 视频联动闭环，正在推进角色语音对话功能**。

## 系统分层

### PYNQ-Z2 侧

- 视频采集与基础预处理（ISP/ROI/缩放）
- 产出 ROI 流并通过 UDP 发送至 310B

### Ascend 310B 侧

- 接收 ROI 包并进行推理调度
- 使用 AscendCL 完成模型加载/执行
- **姿态识别**：YOLOv26s-Pose 模型推理、关键点解码、动作分类 ✅
- **表情识别**：YuNet 人脸检测 + MobileNetV3-Small 6 分类 ✅
- 推理结果经 WebSocket 发送至后端服务
- **角色语音对话**：ASR + LLM + TTS 全流程（进行中）🔄

### 通信层

- ✅ 已具备 PYNQ -> 310B -> PC 三端 UDP 分片链路
- ✅ 已具备控制透传回路

### IP 联动层

- ✅ 采用预生成视频方案（熊二卡通形象）
- ✅ 已实现：视频资产管理、触发逻辑引擎、无缝切换机制
- ✅ 后端事件分发服务（WebSocket :8000）
- ✅ 剧情状态机（欢呼累计触发场景切换）

## 当前进度（2026-04）

### 已完成

#### 智能 IP 互动系统（smart_ip_interaction）✅

- ✅ **姿态识别**：YOLOv26n-Pose 模型，支持挥手/比心/欢呼动作检测
- ✅ **表情识别**：YuNet 人脸检测 + MobileNetV3-Small 6 分类（高兴/惊讶/生气/恐惧/悲伤/中性）
- ✅ **视频联动**：熊二卡通形象互动视频播放，支持待机/挥手/比心/欢呼/表情回应
- ✅ **后端服务**：WebSocket 事件分发 + 剧情状态机（欢呼累计 3 次触发场景切换）
- ✅ **一键启动**：`scripts/launch_pipeline.py` 同时启动推理、后端、播放器
- ✅ **联调测试**：`scripts/launch_local_test.py` 支持模拟数据验证完整链路

#### 角色语音对话（character_voice_chat）🔄

- ✅ ASR 语音识别模块（FunASR）
- ✅ LLM 大语言模型集成
- ✅ TTS 语音合成模块
- 🔄 正在推进：端到端语音对话流程调试

#### 310B 主工程

- ✅ 310B 主程序骨架已推进到可编译状态
- ✅ 已补齐并连通以下模块：
  - 310B 主程序骨架（`code/ascend_310b/src/main.cpp`）
  - 姿态 ROI 协议头（128B 对齐）
  - CMake 构建配置与 aarch64 工具链
  - 构建脚本与 Ascend 环境加载
  - AIPP 静态配置占位文件
- ✅ 已成功产出可执行文件：`ascend310b_pose`

#### 核心算法

- ✅ **姿态识别模型**：已集成 `yolo26s-pose.om`（位于 `code/ascend_310b/yolo26/`）
- ✅ **动作分类逻辑**：已实现基于骨骼关键点的动作判定（挥手/欢呼/未知）
  - 左手/右手腕部高于肩部判定
  - 双手上举判定为欢呼
  - 单手上举判定为挥手
- ✅ **UDP 通信**：完整的三端 UDP 链路（`code/communication/`）

#### 构建与部署

- ✅ 已修复关键构建问题（环境变量、头文件路径、库架构误链、协议头长度断言）
- ✅ 支持命令行参数配置（模型路径、绑定 IP、端口等）

### 当前输入与模型约束

- 输入格式：YUV420SP_U8 (NV12)
- 当前固定输入尺寸：640x640
- 当前聚焦：单模型姿态识别
- 模型产物：已具备 OM（yolo26s-pose.om）

### 待完成（下一阶段）

#### 高优先级 🔴

1. **角色语音对话端到端联调**
   - [ ] 完善 ASR → LLM → TTS 全流程
   - [ ] 集成到智能 IP 互动系统
   - [ ] 实现语音触发视频联动

2. **端到端数据通路**
   - [ ] 推理结果到视频触发的闭环
   - [ ] 延迟补偿与预测边距
   - [ ] 多目标追踪与 ID 管理

#### 中优先级 🟡

3. **性能优化**
   - [ ] 端到端时延压测（目标：<100ms）
   - [ ] 动作识别准确率验证（目标：>90%）
   - [ ] 光照变化鲁棒性测试
   - [ ] 人群密集场景优化

4. **显示输出**
   - [ ] HDMI 本地直显确认
   - [ ] 网络化实时渲染架构对接

### 已知技术债务

- ⚠️ AIPP 配置仅为占位文件，需根据实际模型调整
- ⚠️ 错误处理与日志系统需完善
- ⚠️ 缺少单元测试与集成测试框架

## 里程碑状态

| 里程碑 | 状态 | 说明 |
|--------|------|------|
| M1 通信链路打通 | ✅ 已完成 | PYNQ -> 310B -> PC，含控制透传 |
| M2 姿态识别模块 | ✅ 已完成 | YOLOv26n-Pose + 动作分类 |
| M3 表情识别模块 | ✅ 已完成 | YuNet + MobileNetV3 6 分类 |
| M4 IP 视频联动 | ✅ 已完成 | 熊二卡通形象互动视频播放 |
| M5 角色语音对话 | 🔄 进行中 | ASR + LLM + TTS 端到端联调 |
| M6 端到端时延与稳定性压测 | ⏳ 待开始 | 性能指标验证 |

## 目录说明

```
/workspace/
├── code/
│   ├── pynq_z2/          # PYNQ 侧 FPGA 工程（HLS 图像处理）
│   ├── ascend_310B/      # 310B 推理与控制主工程
│   │   ├── smart_ip_interaction/  # 智能 IP 互动系统（姿态 + 表情 + 视频联动）
│   │   │   ├── run_pipeline.py    # 主推理入口
│   │   │   ├── backend/           # WebSocket 事件分发服务
│   │   │   ├── scripts/           # 启动脚本与测试工具
│   │   │   └── datasets/          # 熊二互动视频资产
│   │   ├── character_voice_chat/  # 角色语音对话（ASR+LLM+TTS）
│   ├── communication/    # 三端 UDP 通信与中继
│   │   └── src/          # pc_udp, 310B_udp, pynq_udp
│   └── ip_player/        # [待创建] IP 视频播放控制
├── docs/                 # 架构与方案文档
├── Scripts/              # 环境与部署辅助脚本
└── README.md             # 本文件
```

## 开发环境

- Ubuntu 22.04 (WSL)
- Vitis 2025.2
- CANN 7.0RC1 (板载预设)
- npu-smi 23.0.rc3
- OpenCV 4.4.0

建议先执行仓库初始化脚本：

```bash
cd Scripts/installation
bash setup.sh
```

Vitis 依赖安装命令：

```bash
/opt/Xilinx/2025.2.1/Vitis/scripts/installLibs.sh
```

说明：Vitis 2025.2.1 下建议使用 vitis-run --mode hls，而非依赖 vitis_hls 命令。

## 快速开始

### 1) 构建通信模块

```bash
cd code/communication
cmake -S . -B build
cmake --build build -j
```

生成目标：pc_udp、310B_udp、pynq_udp。

### 2) 310B 端构建与运行（已验证）

在仓库根目录执行，需先激活专属虚拟环境：

```bash
cd code/ascend_310b
conda activate 310B
bash scripts/build.sh
```

当前状态下该命令已可成功完成构建并生成 `ascend310b_pose`。

运行姿态识别服务：

```bash
./build/ascend310b_pose \
  --model yolo26/yolo26s-pose.om \
  --bind-ip 192.168.2.2 \
  --bind-port 9090 \
  --peer-ip 192.168.137.1 \
  --peer-port 5566
```

### 3) 启动 PC 端（后端 + 播放器）

如果你需要分步启动智能互动系统的后端和本地播放器：

```bash
cd code/ascend_310B/smart_ip_interaction
python backend/server.py

# 【重要】清空代理环境变量（当前终端生效）
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY

python scripts/video_player.py
```

如果你希望**一键同时启动**推理、后端和播放器，也可以直接使用：
```bash
cd code/ascend_310B/smart_ip_interaction
python scripts/launch_pipeline.py
```

> 联调测试（模拟数据，无需真实推流）：
> ```bash
> python scripts/launch_local_test.py
> ```

### 4) PYNQ HLS 综合（可选）

```bash
cd code/pynq_z2
vitis-run --mode hls --tcl hls/HLS_Proj/ROI_Test/scripts/run_hls.tcl
```

### 5) 三端联调建议顺序（仅用于完整 UDP 链路测试）

**注意：智能 IP 互动系统 (smart_ip_interaction) 是独立的 Python 实现，与 pc_udp 互斥。**

- **使用智能 IP 互动系统时** ✅（推荐）：
  使用前述的 PC 端服务和 310B 运行即可，**不需要**启动 `pc_udp`、`310B_udp` 或 `pynq_udp`。

- **测试完整 UDP 链路时**（PYNQ → 310B → PC）：
  1. 启动 PC 端：`code/communication/build/pc_udp`
  2. 启动 310B 中继：`code/communication/build/310B_udp`
  3. 启动 PYNQ 采集：`code/communication/build/pynq_udp`

## 默认网络参数（当前配置）

| 参数 | 值 | 说明 |
|------|-----|------|
| PC_IP | 192.168.137.1 | PC 端 IP |
| ASCEND_310B_IP | 192.168.2.2 | 310B 接收 IP |
| ASCEND_310B_PC_IP | 192.168.137.100 | 310B 发送 IP |
| PYNQ_IP | 192.168.2.99 | PYNQ 端 IP |
| PC_PORT | 5555 | PC 接收端口 |
| PYNQ_PORT | 9090 | PYNQ 发送端口 |
| RESULT_PORT | 5566 | 结果返回端口 |

如需修改，请统一调整 `code/communication/include/udp_common.h`。

## 动作识别协议

### 输出 JSON 格式

```json
{
  "frame_id": 12345,
  "timestamp_ms": 1712345678901,
  "kps_conf_mean": 0.8523,
  "action_flag": 1,
  "action_conf": 0.9124
}
```

### action_flag 说明

| 值 | 动作 | 触发条件 |
|----|------|----------|
| 0 | 未知 | 无有效动作 |
| 1 | 挥手 | 单手手腕高于肩部 |
| 2 | 欢呼 | 双手手腕高于肩部 |
| 3 | 比心 | 双腕靠拢且位于胸前区域 |

## 表情识别协议

### 表情类别

| 值 | 表情 | 说明 |
|----|------|------|
| 0 | happy | 高兴 |
| 1 | surprised | 惊讶 |
| 2 | angry | 生气 |
| 3 | fear | 恐惧 |
| 4 | sad | 悲伤 |
| 5 | neutral | 中性 |

### 识别策略

- 人脸检测：YuNet（ONNX 格式）
- 表情分类：MobileNetV3-Small 6 分类
- 平滑策略：7 帧滑动窗口多数投票

## 文档索引

- 根架构说明：`docs/报告.md`
- 智能 IP 互动：`code/ascend_310B/smart_ip_interaction/README.md`

