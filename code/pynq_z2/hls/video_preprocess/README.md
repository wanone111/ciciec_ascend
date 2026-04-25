# Video Preprocess HLS IP Core

[![HLS](https://img.shields.io/badge/HLS-Video%20Processing-blue)]()
[![Target](https://img.shields.io/badge/Target-PYNQ--Z2-orange)]()
[![Vivado](https://img.shields.io/badge/Vivado-2020.2+-green)]()
[![License](https://img.shields.io/badge/License-MIT-yellow)]()

## 📋 项目概述

**video_preprocess** 是一个面向 PYNQ-Z2 (Zynq-7000) 的高性能 HLS 视频预处理 IP 核，采用模块化设计，将三个关键处理阶段集成到单一流水线中：

1. **轻量级 ISP 处理** - 白平衡 → Gamma 校正 → 时域去噪
2. **动态 ROI 门控** - 基于外部参数的感兴趣区域像素级处理
3. **RGB 到 YUV420SP 转换** - 符合 NV12 标准的 2×2 色度下采样输出

该 IP 采用 AXI4-Stream 接口，可无缝接入 Xilinx 视频处理管线，支持运行时通过 AXI-Lite 接口动态配置分辨率、ROI 参数和 ISP 参数。

```
┌─────────────────────────────────────────────────────────────────┐
│                    Video Preprocess IP Core                      │
│                                                                  │
│  AXI4-Stream          ┌──────────────────────────────────┐       │
│  RGB/BGR Input   ────►│  Stage 1: ISP Processing         │       │
│  (24-bit/pixel)       │  • White Balance (Q8 gain)       │       │
│                       │  • Gamma Correction (4 modes)    │       │
│                       │  • Temporal Denoising            │       │
│                       └──────────────┬───────────────────┘       │
│                                      │                           │
│                       ┌──────────────▼───────────────────┐       │
│                       │  Stage 2: Dynamic ROI Gating     │       │
│                       │  • Pixel-level region check      │       │
│                       │  • ROI-outside pixel zeroing     │       │
│                       └──────────────┬───────────────────┘       │
│                                      │                           │
│                       ┌──────────────▼───────────────────┐       │
│                       │  Stage 3: RGB→YUV420SP (NV12)    │       │
│                       │  • BT.601 color conversion       │       │
│                       │  • 2×2 chroma subsampling        │       │
│                       │  • Dual stream output (Y + UV)   │       │
│                       └──────────────┬───────────────────┘       │
│                                      │                           │
│  AXI4-Stream          ┌──────────────▼───────────────────┐       │
│  Y Stream (8-bit) ◄──┤                                   │       │
│  UV Stream (8-bit) ◄─┤                                   │       │
│  (NV12 interleaved)   └───────────────────────────────────┘       │
│                                                                  │
│  AXI-Lite Control Interface (Runtime Configuration)              │
│  • Resolution (rows, cols)                                       │
│  • ROI Parameters (x, y, w, h)                                   │
│  • ISP Parameters (WB gains, gamma mode, denoise strength)       │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📁 目录结构

```
video_preprocess/
├── README.md                  # 本说明文档
├── README_RGB2YUV.md          # RGB2YUV 转换详细技术文档
├── include/                   # 头文件
│   ├── top.h                  # 顶层模块接口定义
│   ├── isp.h                  # ISP 处理函数（白平衡、Gamma、去噪）
│   ├── roi.h                  # ROI 区域判断函数
│   ├── rgb2yuv.h              # RGB 到 YUV 转换函数
│   └── video_params.h         # 全局参数与常量定义
├── src/                       # 源代码
│   └── top.cpp                # 顶层模块实现（三阶段流水线）
├── tb/                        # 测试平台
│   └── tb_top.cpp             # C 仿真测试用例
└── scripts/                   # 构建脚本
    └── run_hls.tcl            # HLS 综合自动化脚本
```

---

## ⚙️ 核心功能详解

### 1. ISP 处理模块 (`isp.h`)

| 功能 | 描述 | 实现方式 |
|------|------|----------|
| **白平衡** | Q8 定点增益调节，独立 R/G/B 通道 | `apply_gain_q8()` - 单周期乘法移位 |
| **Gamma 校正** | 4 种模式的分段线性近似 | `gamma_map()` - 无 LUT 查找表设计 |
| **时域去噪** | IIR 滤波混合当前帧与前像素 | `denoise_mix()` - 强度 0-255 可调 |

**Gamma 模式：**
- `GAMMA_MODE_BYPASS (0)` - 直通模式
- `GAMMA_MODE_BRIGHT (1)` - 提亮暗部细节
- `GAMMA_MODE_DARK (2)` - 压暗高光区域
- `GAMMA_MODE_DARK_STRONG (3)` - 强对比度增强

### 2. 动态 ROI 模块 (`roi.h`)

```cpp
struct RoiWindow {
    ap_uint<16> x0, y0;  // 左上角坐标
    ap_uint<16> x1, y1;  // 右下角坐标（开区间）
};
```

- **零开销边界检查**：预计算 ROI 窗口结构，使用闭开区间 `[x0, x1) × [y0, y1)`
- **自动钳位**：ROI 参数超出帧范围时自动裁剪到有效区域
- **像素级门控**：ROI 外像素强制清零，同时重置去噪状态避免拖影

### 3. RGB→YUV420SP 转换模块 (`rgb2yuv.h`)

**BT.601 标准系数（Q8.8 定点）：**
```c
Y  = (77*R + 150*G + 29*B) >> 8
U  = (-43*R - 85*G + 128*B) >> 8 + 128
V  = (128*R - 107*G - 21*B) >> 8 + 128
```

**NV12 输出特性：**
- **2×2 色度下采样**：每 2×2 像素块输出 1 个 UV 对
- **双路 AXI4-Stream**：Y 流独立输出，UV 交错输出（U 先 V 后）
- **行对齐保证**：UV 流的 TLAST 在每行结束时置位，便于后续 DMA 传输

---

## 🔧 接口定义

### AXI4-Stream 输入 (`src`)

| 信号 | 位宽 | 描述 |
|------|------|------|
| `data` | 24-bit | RGB/BGR 像素数据 (R:23-16, G:15-8, B:7-0) |
| `keep` | 1-bit | 字节有效指示 |
| `strb` | 1-bit | 选通信号 |
| `user` | 1-bit | 帧起始标志（首像素置 1） |
| `last` | 1-bit | 行结束标志（行末像素置 1） |
| `id` | 1-bit | 通道 ID |
| `dest` | 1-bit | 目标 ID |

### AXI4-Stream 输出 (`y_dst`, `uv_dst`)

| 流 | 位宽 | 格式 | 说明 |
|----|------|------|------|
| `y_dst` | 8-bit | Y 分量 | 每像素输出，`last` 标记行尾 |
| `uv_dst` | 8-bit | UV 交错 | U 和 V 交替输出，`user` 标记 UV 帧起始 |

### AXI-Lite 控制寄存器映射

| 偏移 | 参数 | 位宽 | 默认值 | 说明 |
|------|------|------|--------|------|
| 0x00 | `rows` | 16 | - | 帧高度（1-720） |
| 0x04 | `cols` | 16 | - | 帧宽度（1-1280） |
| 0x08 | `roi_x` | 16 | 0 | ROI 左上角 X |
| 0x0C | `roi_y` | 16 | 0 | ROI 左上角 Y |
| 0x10 | `roi_w` | 16 | 全帧 | ROI 宽度 |
| 0x14 | `roi_h` | 16 | 全帧 | ROI 高度 |
| 0x18 | `wb_r_gain` | 16 | 256 | R 通道增益（Q8，256=1.0x） |
| 0x1A | `wb_g_gain` | 16 | 256 | G 通道增益 |
| 0x1C | `wb_b_gain` | 16 | 256 | B 通道增益 |
| 0x1E | `gamma_mode` | 8 | 0 | Gamma 模式选择 |
| 0x1F | `denoise_strength` | 8 | 0 | 去噪强度（0-255） |
| 0x20 | `input_is_bgr` | 8 | 0 | 输入格式（1=BGR, 0=RGB） |

---

## 🚀 快速开始

### 环境要求

- **Vivado HLS** 2020.2 或更高版本
- **Vitis HLS** 2021.1 或更高版本
- 目标器件：`xc7z020clg400-1` (PYNQ-Z2)

### 运行 HLS 综合

```bash
cd /workspace/hls/video_preprocess

# 方法 1: 使用 vitis-run (Vitis HLS)
vitis-run --mode hls --tcl scripts/run_hls.tcl

# 方法 2: 使用 vivado_hls (Vivado HLS)
vivado_hls -f scripts/run_hls.tcl
```

### 运行 C 仿真

编辑 `scripts/run_hls.tcl`，取消注释以下行：
```tcl
csim_design
```

然后重新运行综合脚本。

### 查看综合报告

综合完成后，检查资源使用和时序报告：
```bash
cat video_preprocess/solution1/syn/report/csynth.rpt
```

### 导出 IP Catalog

如需在 Vivado Block Design 中使用，编辑 `scripts/run_hls.tcl`：
```tcl
export_design -format ip_catalog -output ./ip_output
```

---

## 📊 性能指标

以下为典型配置下的综合估算值（实际结果以 HLS 报告为准）：

| 指标 | 估算值 | 备注 |
|------|--------|------|
| **LUT** | 8,000 - 12,000 | 包含 DSP48 控制逻辑 |
| **FF** | 6,000 - 9,000 | 线缓冲与流水线寄存器 |
| **BRAM** | 10 - 15 | 用于行缓存（可推断为 SRL） |
| **DSP48** | 6 - 9 | YUV 转换并行计算单元 |
| **延迟** | 2 - 3 行 | 从输入到输出的流水线深度 |
| **吞吐率** | 1 像素/时钟 | II=1，满足实时视频处理需求 |
| **最大频率** | ~100 MHz | 取决于具体约束与优化 |

**实时性分析：**
- 1280×720@60fps 需要 55.3M pixels/s
- 在 100MHz 时钟下，理论吞吐率为 100M pixels/s
- 余量充足，可支持更高分辨率或帧率

---

## 🔬 优化策略

### 1. DATAFLOW 任务级并行

```cpp
#pragma HLS DATAFLOW
```
三个处理阶段（ISP → ROI → YUV）在不同数据上并行执行，最大化吞吐量。

### 2. 循环流水线优化

```cpp
#pragma HLS PIPELINE II=1 rewind
```
内层像素循环实现 Initiation Interval = 1，确保每个时钟周期处理一个像素。

### 3. SRL 推断的行缓存

```cpp
#pragma HLS BIND_STORAGE variable=prev_row_r type=ram_2p
#pragma HLS ARRAY_PARTITION variable=prev_row_r cyclic factor=8 dim=1
```
使用分布式 RAM（SRL）而非 BRAM，降低访问延迟至 1 周期。

### 4. 并行 DSP48 YUV 计算

```cpp
#pragma HLS PIPELINE II=1
```
Y/U/V 三路转换并行执行，利用 DSP48 的乘加单元加速。

---

## 🛠️ 集成到 Vivado 工程

### 步骤 1: 添加 IP 到仓库

```bash
# 导出 IP Catalog 后
cp -r ip_output/* <your_vivado_project>/ip_repo/
```

### 步骤 2: 在 Block Design 中实例化

1. 打开 Vivado，进入 **IP Catalog**
2. 右键 **Repository Settings**，添加上述路径
3. 搜索 "video_preprocess"，拖入 Block Design

### 步骤 3: 连接系统

```
┌─────────────┐     AXI4-Stream     ┌──────────────────┐
│  Video In   │────────────────────►│ video_preprocess │
│  (CSI/DMA)  │                     │                  │
└─────────────┘                     │   Y Stream ──────┼──► DMA/YUV Encoder
                                  │   UV Stream ─────┤
┌─────────────┐     AXI-Lite        │                  │
│  Zynq PS    │◄────────────────────│                  │
│  (GPIO/I2C) │   Control Registers └──────────────────┘
└─────────────┘
```

### 步骤 4: 地址分配与导出

1. 点击 **Run Address Automation** 分配 AXI-Lite 基地址
2. 生成 HDL Wrapper
3. 执行综合与实现

---

## 🧪 测试验证

### C 仿真测试用例

测试平台 (`tb/tb_top.cpp`) 验证以下场景：

- **输入格式**：640×480 RGB 渐变图案
- **ROI 配置**：中心 320×240 区域
- **ISP 参数**：默认直通模式
- **输出验证**：
  - Y 流包计数 = 640×480 = 307,200
  - UV 流包计数 = (480/2)×640 = 153,600
  - `user`/`last` 标志正确性检查

### 硬件在环测试

建议在 PYNQ-Z2 上使用以下 Python 脚本验证：

```python
from pynq import Overlay
import numpy as np

overlay = Overlay("video_preprocess.bit")
vp = overlay.video_preprocess

# 配置参数
vp.rows = 480
vp.cols = 640
vp.roi_x = 160
vp.roi_y = 120
vp.roi_w = 320
vp.roi_h = 240
vp.wb_r_gain = 256
vp.gamma_mode = 0

# 启动 DMA 传输并捕获输出
# ... (省略 DMA 配置代码)
```

---

## 📝 常见问题

### Q1: 如何调整最大分辨率？

修改 `include/video_params.h` 中的常量：
```cpp
static const int MAX_WIDTH = 1920;   // 改为 1080p
static const int MAX_HEIGHT = 1080;
```
**注意**：增加分辨率会提高 BRAM 消耗（行缓存增大）。

### Q2: ROI 可以动态更新吗？

是的，通过 AXI-Lite 接口实时更新 `roi_x/y/w/h` 寄存器即可。建议在垂直消隐期间（VBLANK）更新以避免撕裂。

### Q3: 如何禁用某个 ISP 功能？

- **白平衡**：设置 `wb_r_gain = wb_g_gain = wb_b_gain = 256`
- **Gamma**：设置 `gamma_mode = GAMMA_MODE_BYPASS`
- **去噪**：设置 `denoise_strength = 0`

### Q4: UV 输出为什么比 Y 少一半？

NV12 格式采用 2×2 色度下采样，每 4 个 Y 像素共享 1 个 UV 对，这是 YUV420SP 的标准规范。

---

## 📄 许可证

本项目采用 **MIT License**，详见 LICENSE 文件。

---

## 👥 贡献指南

欢迎提交 Issue 和 Pull Request！主要改进方向：

- [ ] 添加更多 Gamma 曲线模式
- [ ] 支持空域去噪（中值/双边滤波）
- [ ] 扩展 ROI 形状（圆形/多边形）
- [ ] 添加 HDR 色调映射模块
- [ ] 优化 BRAM 使用效率

---

## 📧 联系方式

如有问题或合作意向，请通过 GitHub Issues 联系。

---

**最后更新**: 2025 年 4 月
**维护者**: PYNQ-Z2 Video Processing Team