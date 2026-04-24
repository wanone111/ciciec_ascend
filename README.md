# 集创赛 华强x昇腾赛题

## 项目目标

基于 PYNQ-Z2 (FPGA) + Ascend 310B (NPU) 构建异构视觉交互终端，面向园区/展演场景实现：

- 动作识别（挥手/比心/欢呼）
- 多目标管理与交互决策
- 前后端联动控制（优先 UDP）

当前阶段目标已收敛为：先打通 310B 端单模型姿态识别闭环，再扩展表情分支。

## 系统分层

### PYNQ-Z2 侧

- 视频采集与基础预处理（ISP/ROI/缩放）
- 产出 ROI 流并通过 UDP 发送至 310B

### Ascend 310B 侧

- 接收 ROI 包并进行推理调度
- 使用 AscendCL 完成模型加载/执行骨架
- 推理结果经 UDP 回传控制链路

### 通信层

- 已具备 PYNQ -> 310B -> PC 三端 UDP 分片链路
- 已具备控制透传回路

## 当前进度（2026-04）

### 已完成

- 310B 主工程从空壳推进到可编译状态
- 已补齐并连通以下模块：
  - 310B 主程序骨架
  - 姿态 ROI 协议头（128B 对齐）
  - CMake 构建配置与 aarch64 工具链
  - 构建脚本与 Ascend 环境加载
  - AIPP 静态配置占位文件
- 已修复关键构建问题（环境变量、头文件路径、库架构误链、协议头长度断言）
- 已成功产出可执行文件：ascend310b_pose

### 当前输入与模型约束

- 输入格式：YUV420SP_U8 (NV12)
- 当前固定输入尺寸：640x640
- 当前聚焦：单模型姿态识别
- 模型产物：已具备 OM（yolo26s-pose.om）

### 待完成（下一阶段）

- 接入真实 AscendCL 运行时推理闭环（非仅骨架）
- 完成与 PYNQ 侧 ROI NV12 协议实流对接
- 完成动作后处理与稳定输出字段固化
- 进行端到端时延与稳定性压测

## 里程碑状态

- M1 通信链路打通：已完成（PYNQ -> 310B -> PC，含控制透传）
- M2 310B 工程可编译：已完成（生成 ascend310b_pose）
- M3 单模型实流推理闭环：进行中
- M4 端到端时延与稳定性压测：待开始

## 目录说明

- code/pynq_z2: PYNQ 侧工程
- code/ascend_310b: 310B 推理与控制主工程
- code/communication: 三端 UDP 通信与中继
- docs: 架构与方案文档
- Scripts: 环境与部署辅助脚本

## 开发环境

- Ubuntu 22.04 (WSL)
- Vitis 2025.2
- CANN 7.0RC1(板载预设)
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

## 310B 构建（已验证）

在仓库根目录执行：

```bash
cd code/ascend_310b
bash scripts/build.sh
```

当前状态下该命令已可成功完成构建并生成 ascend310b_pose。

### 3) PYNQ HLS 综合（可选）

```bash
cd code/pynq_z2
vitis-run --mode hls --tcl hls/HLS_Proj/ROI_Test/scripts/run_hls.tcl
```

### 4) 三端联调建议顺序

1. 启动 PC 端：code/communication/build/pc_udp
2. 启动 310B 中继：code/communication/build/310B_udp
3. 启动 PYNQ 采集：code/communication/build/pynq_udp

## 默认网络参数（当前配置）

- PC_IP: 192.168.137.1
- ASCEND_310B_IP: 192.168.2.2
- ASCEND_310B_PC_IP: 192.168.137.100
- PYNQ_IP: 192.168.2.99
- PC_PORT: 5555
- PYNQ_PORT: 9090

如需修改，请统一调整 code/communication/include/udp_common.h。

## 文档索引

- 根架构说明：docs/报告.md
- 310B 子模块：code/ascend_310b/README.md
- 通信链路：code/communication/README.md
- PYNQ 子模块：code/pynq_z2/README.md
- 脚本目录：Scripts/README.md
