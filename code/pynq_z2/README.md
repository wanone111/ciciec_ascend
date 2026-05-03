# PYNQ-Z2 前端视频预处理工程

本目录保存 PYNQ-Z2 侧 FPGA/PL 相关工程，用于智能视觉交互系统的前端视频预处理。当前主工程是 `hls/video_preprocess/`，目标是在视频进入 Ascend 310B 推理链路前完成轻量 ISP、动态 ROI 门控和 RGB/BGR 到 NV12 的格式整理。

## 当前结构

```text
code/pynq_z2/
├── README.md
├── hls/
│   ├── video_preprocess/
│   └── HLS_Proj/
├── pl/
│   ├── README.md
│   └── bit/
│       ├── hdmi_vdma_bd_v2.bit
│       └── hdmi_vdma_bd_v2.hwh
└── vivado/
    └── xdc/
        ├── PYNQ-Z2 v1.0.tcl
        └── PYNQ-Z2 v1.0.xdc
```

说明：

- `hls/video_preprocess/` 是当前推荐维护的 HLS IP 工程。
- `hls/HLS_Proj/` 保存历史实验工程和模块验证工程。
- `pl/bit/` 保存可上板使用的 `.bit` 和 `.hwh` 文件。
- `vivado/xdc/` 保存 PYNQ-Z2 约束文件。

## 主 HLS IP

顶层函数：`video_preprocess`

位置：

```text
hls/video_preprocess/include/top.h
hls/video_preprocess/src/top.cpp
```

处理链路：

```text
AXI4-Stream RGB/BGR 输入
        |
        v
ISP: 白平衡 + Gamma + 时域降噪
        |
        v
动态 ROI 像素级门控
        |
        v
RGB 到 YUV420SP / NV12
        |
        v
Y 流 + UV 交错流双 AXI4-Stream 输出
```

顶层接口简表：

| 接口 | 类型 | 说明 |
|---|---|---|
| `src` | AXI4-Stream 24-bit | RGB/BGR 输入 |
| `y_dst` | AXI4-Stream 8-bit | Y 亮度输出 |
| `uv_dst` | AXI4-Stream 8-bit | UV 交错输出 |
| `rows` / `cols` | AXI-Lite | 帧高和帧宽 |
| `roi_x/y/w/h` | AXI-Lite | ROI 窗口 |
| `wb_*_gain` | AXI-Lite | Q8 白平衡增益，256 为 1.0x |
| `gamma_mode` | AXI-Lite | 0 bypass，1 bright，2 dark，3 dark strong |
| `denoise_strength` | AXI-Lite | 0 到 255 |
| `input_is_bgr` | AXI-Lite | 1 表示 BGR，0 表示 RGB |

## 构建 HLS IP

先加载 Vitis/Vivado 环境，再进入主工程目录：

```bash
source /opt/Xilinx/2025.2.1/Vitis/settings64.sh
cd /home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/video_preprocess
vitis-run --mode hls --tcl scripts/run_hls.tcl
```

脚本会自动推导工程路径，并输出到：

```text
hls/video_preprocess/build/hls_video_preprocess/
hls/video_preprocess/ipcore_file/video_preprocess/
```

`scripts/run_hls.tcl` 当前默认执行：

- C simulation
- C synthesis
- C/RTL co-simulation
- IP catalog export

OpenCV 是可选依赖。脚本会按 `OPENCV_ROOT`、`CONDA_PREFIX` 和常见路径检测；找不到时 testbench 使用纯 C++ 路径。

## 已知综合基线

当前文档记录的基线来自 `hls/video_preprocess` 版本：

| 指标 | 数值 |
|---|---|
| 目标器件 | `xc7z020clg400-1` |
| 目标时钟 | 10 ns |
| 估计时钟 | 7.268 ns |
| 资源状态 | BRAM/DSP/LUT/FF 占用较低，满足后续联调余量 |

实际结果以本机重新运行 HLS 后的报告为准。

## 与 Ascend 310B 的关系

PYNQ-Z2 负责前端图像整理，Ascend 310B 负责姿态/表情推理和事件生成。当前仓库的业务演示链路主要通过 `code/communication` 的 UDP 分片协议把视频流送到 310B，并由 `smart_ip_interaction/run_pipeline.py` 进行推理。

相关目录：

- 通信协议与 pybind11 接口：`../communication/`
- 310B 视觉互动主链路：`../ascend_310B/smart_ip_interaction/`
- 顶层项目说明：`../../README.md`

## 注意事项

- 运行 `vitis-run` 前必须加载 Vitis 环境。
- 本目录没有 PS 侧 Python 控制脚本；当前可用上板产物主要在 `pl/bit/`。
- `hls/HLS_Proj/` 下的工程是历史/实验工程，不是当前推荐主线。
- 上板前需要确认 `.bit` 和 `.hwh` 文件名一致，并与 PYNQ overlay 加载路径匹配。
