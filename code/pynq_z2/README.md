# PYNQ-Z2 视频预处理系统

[![Platform](https://img.shields.io/badge/Platform-PYNQ--Z2-orange)](https://www.xilinx.com/products/boards-and-kits/pynq-z2.html)
[![HLS](https://img.shields.io/badge/HLS-Vitis-blue)](https://www.xilinx.com/products/design-tools/vitis.html)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)

## 📋 项目概述

本项目是基于 **PYNQ-Z2 FPGA** 的视频流前端预处理系统，专为华为昇腾 310B 异构计算平台设计。主工程位于 `hls/video_preprocess/`，实现了 **ISP 图像处理**、**动态 ROI 区域选择** 和 **RGB 到 YUV420SP 转换** 三大核心功能，与 310B NPU 协同完成智能视觉交互任务。

### 应用场景

- 🎭 主题乐园 IP 沉浸式演艺联动
- 🎯 动作识别（挥手、欢呼、比心等）
- 😊 表情捕捉与响应
- 📹 复杂环境下的视频预处理

### 当前状态快照（2026-04-26）

- 主工程：`hls/video_preprocess/`
- CSim：已通过（10/10 用例通过）
- C Synthesis：已完成并生成报告（`video_preprocess_csynth.rpt`）
- 关键综合指标（目标器件 `xc7z020-clg400-1`）：时钟估计 7.268 ns（目标 10 ns）
- 资源占用：BRAM 30 (10%)，DSP 14 (6%)，FF 7386 (6%)，LUT 7779 (14%)
- 当前待持续推进：长时 RTL/Cosim 稳定跑通、板级吞吐与端到端延迟实测闭环

### 当前阻塞与注意事项

- 运行 `vitis-run` 前必须先加载 Vitis 环境，否则会出现 `vitis-run: command not found`。
- 在 conda 环境下可能出现 `libtinfo.so.6: no version information available`，当前属于告警，不影响 CSim/CSynth 结果。
- 现阶段工具链输出中有若干 HLS warning（如 INLINE/PIPELINE pragma 冲突、可变 tripcount 循环无法完全 unroll），不阻塞综合，但会影响后续性能收敛与可维护性。

---

## 🏗️ 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        感知输入层                                │
│                    USB/CMOS 摄像头采集                            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      PYNQ-Z2 (FPGA)                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │  ROI 选择   │→ │  ISP 处理   │→ │  RGB2YUV420SP 输出      │ │
│  │  (动态配置) │  │(白平衡/Gamma)│  │    (至 310B DVPP)      │ │
│  │             │  │  + 降噪     │  │                         │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                       昇腾 310B                                 │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │   DVPP      │→ │    AIPP     │→ │      NPU 推理           │ │
│  │ (缩放/裁剪) │  │(归一化/转换)│  │  (动作识别/表情捕捉)    │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                       沉浸表现层                                │
│              HDMI 输出 / 网络推送 / 剧情联动                     │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📁 目录结构

```
/workspace
├── README.md                 # 项目说明文档（本文件）
├── 赛题.md                   # 赛题需求详细说明
├── hls/                      # HLS 高层次综合工程
│   ├── video_preprocess/     # ★ 主工程目录 (ISP + ROI + RGB2YUV)
│   │   ├── include/          # 头文件定义 (top.h, isp.h, roi.h, rgb2yuv.h)
│   │   ├── src/              # 源代码实现 (top.cpp)
│   │   ├── tb/               # Testbench 测试
│   │   ├── scripts/          # TCL 构建脚本
│   │   └── README.md         # 详细设计文档
│   ├── HLS_Proj/             # 历史工程备份
│   ├── test3_dynamic_roi/    # 动态 ROI 独立测试
│   └── HLS_Proj_Temp/        # 临时备份目录
├── ps/                       # PS 侧 Python 脚本
│   ├── usb_camera_to_pl.py   # USB 摄像头→PL 桥接主脚本
│   ├── usb_camera.py         # 摄像头检测工具
│   └── PL_ROI_NoDHMI.py      # 无 HDMI 输出模式脚本
├── pl/                       # PL 比特流文件
│   ├── README.md             # 比特流使用说明
│   └── pl_roi_test/          # ROI 测试比特流
├── vivado/                   # Vivado 工程约束
│   └── xdc/                  # XDC 时序约束文件
└── ipcore_file/              # 封装 IP 核文件
```

---

## ⚙️ HLS 内核功能

### 顶层接口定义

```cpp
void video_preprocess(
    hls::stream<axi_rgb_t>& src,      // AXI4-Stream 输入 (24-bit RGB/BGR)
    hls::stream<axi_u8_t>& y_dst,     // AXI4-Stream 输出 Y (luma)
    hls::stream<axi_u8_t>& uv_dst,    // AXI4-Stream 输出 UV (chroma interleaved)
    ap_uint<16> rows,                 // 图像高度
    ap_uint<16> cols,                 // 图像宽度
    ap_uint<16> roi_x,                // ROI 左上角 X 坐标
    ap_uint<16> roi_y,                // ROI 左上角 Y 坐标
    ap_uint<16> roi_w,                // ROI 宽度
    ap_uint<16> roi_h,                // ROI 高度
    ap_uint<16> wb_r_gain,            // 白平衡 R 增益 (Q8 定点，256=1.0x)
    ap_uint<16> wb_g_gain,            // 白平衡 G 增益 (Q8 定点，256=1.0x)
    ap_uint<16> wb_b_gain,            // 白平衡 B 增益 (Q8 定点，256=1.0x)
    ap_uint<8> gamma_mode,            // Gamma 模式：0=BYPASS, 1=BRIGHT, 2=DARK, 3=DARK_STRONG
    ap_uint<8> denoise_strength,      // 降噪强度 (0-255)
    ap_uint<8> input_is_bgr           // 输入格式：1=BGR, 0=RGB
);
```

### 处理流水线

```
┌─────────────┐    ┌─────────────┐    ┌─────────────────┐    ┌──────────────┐
│  像素输入   │ →  │  ROI 判断   │ →  │   ISP 处理      │ →  │  RGB2YUV420SP│
│ (AXI-Stream)│    │ (动态配置)  │    │ 白平衡+Gamma+降噪│    │  (NV12 输出) │
└─────────────┘    └─────────────┘    └─────────────────┘    └──────────────┘
                        │
                        └─→ ROI 外区域：输出清零，重置降噪状态
```

### 核心功能模块

| 模块 | 功能描述 | 配置参数 |
|------|---------|---------|
| **ISP 处理** | 白平衡 + Gamma 校正 + 时域降噪 | `wb_r/g/b_gain`, `gamma_mode`, `denoise_strength` |
| **动态 ROI** | 像素级门控，仅 ROI 内执行 ISP | `roi_x/y/w/h` |
| **RGB2YUV** | RGB 到 YUV420SP (NV12) 转换，2x2 色度抽取 | 自动处理，无需配置 |
| **格式适配** | 支持 RGB/BGR 输入切换 | `input_is_bgr` |

### 优化策略

- **DATAFLOW**：三阶段任务级并行（ISP → ROI → RGB2YUV）
- **SRL 线缓冲**：2x2 色度平均，1 周期访问延迟
- **DSP48 并行计算**：YUV 转换加速，延迟降低 15-25%
- **零开销 ROI 检查**：预计算边界，无分支预测惩罚

### 当前优化方向（团队对齐）

#### P0：稳定性与可复现性（优先）

- 将构建流程拆分为独立目标：`csim` / `csynth` / `cosim`，避免每次默认跑完整链路导致调试周期过长。
- 固化统一启动方式：先 `source /opt/Xilinx/2025.2.1/Vitis/settings64.sh`，再执行 `vitis-run`。
- 在脚本中显式打印工具版本、路径和关键依赖（OpenCV、clang）以降低环境偏差。

#### P1：性能收敛（近期）

- 消除 pragma 冲突：避免同一函数同时要求不可兼容的 `INLINE` 与 `PIPELINE` 组合。
- 减少可变 tripcount 带来的调度不确定性：将关键循环改为“固定上限 + 有效区间判断”模式。
- 进一步压缩 `stage_isp_processing` 的 LUT 占用，优先优化像素索引与分支路径。

#### P2：系统联调（并行）

- 完成板级实测指标补齐：`fps`、帧内延迟、ROI 切换时延、UV 对齐正确性。
- 与 310B 侧对齐 NV12 数据契约，避免重复色彩转换和冗余预处理。
- 输出统一版本的联调验收清单（输入格式、分辨率、寄存器配置、日志关键字）。

### 资源约束

- 最大分辨率：**1280 × 720** @ 60fps
- 数据位宽：24-bit RGB 输入，8-bit Y/UV 输出
- 控制接口：AXI-Lite 寄存器映射
- 输出格式：YUV420SP (NV12)，双 AXI-Stream（Y + UV 分离）

---

## 🚀 快速开始

### 前置条件

- Xilinx Vitis/Vivado 2020.2 或更高版本
- PYNQ-Z2 开发板
- Python 3.6+ (PS 侧脚本)
- OpenCV (摄像头采集)

### HLS 综合流程

```bash
# 进入主工程目录
cd /workspace/hls/video_preprocess

# 加载 Vitis 环境（必须）
source /opt/Xilinx/2025.2.1/Vitis/settings64.sh

# 执行 HLS 一键综合
vitis-run --mode hls --tcl scripts/run_hls.tcl

# 综合结果输出至：
# build/hls_video_preprocess/
# 生成的 IP 核可用于 Vivado 集成
```

### PS 侧脚本使用

#### 1. 摄像头能力探测

```bash
python3 ps/usb_camera_to_pl.py --probe --camera 0
```

#### 2. Dry-run 模式验证（无板卡）

```bash
python3 ps/usb_camera_to_pl.py \
    --dry-run \
    --camera 0 \
    --width 640 \
    --height 480 \
    --show
```

#### 3. 上板运行

```bash
python3 ps/usb_camera_to_pl.py \
    --overlay bitstream/pl_roi_test.bit \
    --ip-name video_preprocess \
    --camera 0 \
    --width 640 \
    --height 480 \
    --roi 0.1,0.1,0.8,0.8 \
    --wb-gain 256,256,256 \
    --gamma-mode 0 \
    --show
```

#### 参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--camera` | 摄像头设备 ID | 0 |
| `--width/height` | 输入分辨率 | 640x480 |
| `--roi` | ROI 区域 (x,y,w,h 归一化) | 全图 |
| `--wb-gain` | 白平衡增益 (R,G,B) | 256,256,256 |
| `--gamma-mode` | Gamma 模式 (0-3) | 0 (Bypass) |
| `--dry-run` | 无板卡模拟模式 | - |
| `--show` | 显示输出预览 | - |

---

## 🔧 与 310B 协同设计

### 分工原则

| 处理阶段 | PYNQ-Z2 职责 | 310B 职责 |
|---------|-------------|----------|
| **几何处理** | ROI 动态裁剪 | DVPP 缩放/裁剪 |
| **色彩处理** | 白平衡、Gamma | AIPP 色域转换 |
| **数学预处理** | - | AIPP 归一化/减均值 |
| **推理计算** | - | NPU 深度学习推理 |

### 数据流规范

```
PYNQ-Z2 输出 → 310B DVPP 输入
格式：YUV420SP (NV12)
协议：AXI4-Stream (双路：Y + UV 分离)
```

> ⚠️ **注意**：PYNQ-Z2 已输出 YUV420SP 格式，310B DVPP 可直接用于后续缩放/裁剪，无需重复色域转换。

---

## 📊 性能指标

| 指标 | 目标值 | 实测值 |
|------|--------|--------|
| 综合时钟估计 | <= 10ns | 7.268ns |
| BRAM 占用 | < 20% | 10% |
| LUT 占用 | < 20% | 14% |
| DSP 占用 | < 20% | 6% |
| 输入延迟 | < 16ms (60fps) | 板测待补齐 |
| ROI 切换延迟 | < 1ms | TBD |
| 吞吐率 | 1280×720@60fps | TBD |
| 动作识别准确率 | > 90% | - |
| 表情响应延迟 | < 100ms | - |

---

## 🛠️ 开发与调试

### Testbench 验证

```bash
cd hls/video_preprocess
make run
```

### C 仿真验证

```bash
cd hls/video_preprocess
vitis-run --mode csim --tcl scripts/run_hls.tcl
```

### 波形调试

综合完成后在 Vivado 中打开：
```
build/hls_video_preprocess/solution1/sim/verilog/*.vcd
```

### 寄存器映射表

| 偏移地址 | 寄存器名 | 位宽 | 描述 |
|---------|---------|------|------|
| 0x00 | ROWS | 16 | 图像高度 |
| 0x04 | COLS | 16 | 图像宽度 |
| 0x08 | ROI_X | 16 | ROI 左上角 X 坐标 |
| 0x0C | ROI_Y | 16 | ROI 左上角 Y 坐标 |
| 0x10 | ROI_W | 16 | ROI 宽度 |
| 0x14 | ROI_H | 16 | ROI 高度 |
| 0x18 | WB_R_GAIN | 16 | 白平衡 R 通道增益 (Q8) |
| 0x1A | WB_G_GAIN | 16 | 白平衡 G 通道增益 (Q8) |
| 0x1C | WB_B_GAIN | 16 | 白平衡 B 通道增益 (Q8) |
| 0x1E | GAMMA_MODE | 8 | Gamma 模式 (0-3) |
| 0x1F | DENOISE_STRENGTH | 8 | 降噪强度 (0-255) |
| 0x20 | INPUT_IS_BGR | 8 | 输入格式 (1=BGR, 0=RGB) |


---

## 🤝 贡献指南

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

---

## 📄 许可证

本项目采用 MIT 许可证 - 查看 [LICENSE](LICENSE) 文件了解详情。

---

## 📞 联系方式

- 项目负责人：[待填写]
- 技术支持：[待填写]
- 问题反馈：请通过 GitHub Issues 提交

---

## 🔗 参考资料

- [Xilinx Vitis HLS 官方文档](https://www.xilinx.com/products/design-tools/vitis.html)
- [PYNQ-Z2 用户手册](http://www.pynq.io/)
- [华为昇腾 DVPP 开发指南](https://support.huawei.com/enterprise/zh/doc/EDOC1100196496)
- [AMD Vitis Libraries](https://github.com/Xilinx/Vitis_Libraries)

---

<div align="center">

**🎯 让视觉交互更智能 · 让 IP 演艺更沉浸**

Made with ❤️ by Competition Team

</div>