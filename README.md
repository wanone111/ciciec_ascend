# 集创赛 华强 x 昇腾赛题

## 项目目标

基于 PYNQ-Z2 (FPGA) + Ascend 310B (NPU) 构建异构视觉交互终端，面向园区/展演场景实现：

- **动作识别**（挥手/比心/欢呼）
- **表情捕捉与识别**
- **多目标管理与交互决策**
- **IP 联动演艺**（预生成视频触发）
- **前后端联动控制**（优先 UDP）

当前阶段目标：完成姿态识别闭环，扩展表情分支，实现 IP 视频联动。

## 系统分层

### PYNQ-Z2 侧

- 视频采集与基础预处理（ISP/ROI/缩放）
- 产出 ROI 流并通过 UDP 发送至 310B

### Ascend 310B 侧

- 接收 ROI 包并进行推理调度
- 使用 AscendCL 完成模型加载/执行
- **姿态识别**：YOLOv26s-Pose 模型推理、关键点解码、动作分类 ✅
- **表情识别**：待实现（人脸检测 + 表情分类网络）
- 推理结果经 UDP 回传控制链路

### 通信层

- ✅ 已具备 PYNQ -> 310B -> PC 三端 UDP 分片链路
- ✅ 已具备控制透传回路

### IP 联动层

- 采用预生成视频方案（Coze 生成）
- 待实现：视频资产管理、触发逻辑引擎、无缝切换机制

## 当前进度（2026-04）

### 已完成

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

1. **表情捕捉模块**
   - [ ] 人脸检测模型（OM 格式）
   - [ ] DVPP ROI 处理与 AIPP 配置集成
   - [ ] 表情分类网络（7 类基本表情）
   - [ ] 多任务调度（姿态 + 表情并发推理）

2. **IP 视频联动**
   - [ ] 视频资产管理（3 种卡通熊 IP 主题）
   - [ ] 触发逻辑引擎（动作/表情 → 视频 ID 映射）
   - [ ] 视频播放控制（GStreamer/VLC 集成）
   - [ ] 无缝切换机制（避免卡顿/黑屏）

3. **端到端数据通路**
   - [ ] 推理结果到视频触发的闭环
   - [ ] 延迟补偿与预测边距
   - [ ] 多目标追踪与 ID 管理

#### 中优先级 🟡

4. **性能优化**
   - [ ] 端到端时延压测（目标：<100ms）
   - [ ] 动作识别准确率验证（目标：>90%）
   - [ ] 光照变化鲁棒性测试
   - [ ] 人群密集场景优化

5. **显示输出**
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
| M2 310B 工程可编译 | ✅ 已完成 | 生成 ascend310b_pose |
| M3 单模型实流推理闭环 | 🔄 进行中 | 姿态识别闭环待联调 |
| M4 表情识别模块 | ⏳ 待开始 | 人脸检测 + 表情分类 |
| M5 IP 视频联动 | ⏳ 待开始 | 预生成视频触发 |
| M6 端到端时延与稳定性压测 | ⏳ 待开始 | 性能指标验证 |

## 目录说明

```
/workspace/
├── code/
│   ├── pynq_z2/          # PYNQ 侧 FPGA 工程（HLS 图像处理）
│   ├── ascend_310b/      # 310B 推理与控制主工程
│   │   ├── src/          # 主程序（姿态识别完整实现）
│   │   ├── include/      # 协议头定义
│   │   ├── yolo26/       # YOLOv26s-Pose 模型（om/pt）
│   │   └── config/       # AIPP 配置文件
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

### 2) 310B 构建（已验证）

在仓库根目录执行：

```bash
cd code/ascend_310b
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

### 3) PYNQ HLS 综合（可选）

```bash
cd code/pynq_z2
vitis-run --mode hls --tcl hls/HLS_Proj/ROI_Test/scripts/run_hls.tcl
```

### 4) 三端联调建议顺序

1. 启动 PC 端：`code/communication/build/pc_udp`
2. 启动 310B 中继：`code/communication/build/310B_udp`
3. 启动 PYNQ 采集：`code/communication/build/pynq_udp`
4. 启动 310B 推理服务：`code/ascend_310b/build/ascend310b_pose`

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

## 文档索引

- 根架构说明：`docs/报告.md`
- 310B 子模块：`code/ascend_310b/README.md`
- 通信链路：`code/communication/README.md`
- PYNQ 子模块：`code/pynq_z2/README.md`
- 脚本目录：`Scripts/README.md`

## 贡献指南

1. Fork 本仓库
2. 创建功能分支 (`git checkout -b feature/amazing-feature`)
3. 提交变更 (`git commit -m 'Add amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 提交 Pull Request

## 许可证

本项目遵循 MIT 许可证。