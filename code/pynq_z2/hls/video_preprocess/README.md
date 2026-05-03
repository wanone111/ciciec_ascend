# video_preprocess HLS IP

`video_preprocess` 是 PYNQ-Z2 前端视频预处理 HLS IP。它接收 AXI4-Stream RGB/BGR 视频流，执行轻量 ISP 和动态 ROI 门控，并输出 YUV420SP/NV12 格式的 Y 流与 UV 交错流，供后续 DMA、UDP 封装或 Ascend 310B 预处理链路使用。

## 功能概览

```text
AXI4-Stream RGB/BGR
        |
        v
Stage 1: ISP + ROI
  - 白平衡 Q8 增益
  - Gamma 模式
  - 简单时域降噪
  - ROI 内处理，ROI 外清零
        |
        v
Stage 2: RGB2YUV420SP
  - BT.601 定点转换
  - 2x2 色度下采样
  - Y / UV 双 AXI4-Stream 输出
```

## 目录结构

```text
video_preprocess/
├── README.md
├── README_RGB2YUV.md
├── include/
│   ├── isp.h
│   ├── rgb2yuv.h
│   ├── rgb2yuv_optimized.h
│   ├── roi.h
│   ├── top.h
│   └── video_params.h
├── scripts/
│   └── run_hls.tcl
├── src/
│   ├── top.cpp
│   └── top_realtime_60fps_v8.cpp
├── tb/
│   └── tb_top.cpp
└── ipcore_file/
```

## 顶层接口

定义位置：`include/top.h`

```cpp
void video_preprocess(
    hls::stream<axi_rgb_t>& src,
    hls::stream<axi_u8_t>& y_dst,
    hls::stream<axi_u8_t>& uv_dst,
    ap_uint<16> rows,
    ap_uint<16> cols,
    ap_uint<16> roi_x,
    ap_uint<16> roi_y,
    ap_uint<16> roi_w,
    ap_uint<16> roi_h,
    ap_uint<16> wb_r_gain,
    ap_uint<16> wb_g_gain,
    ap_uint<16> wb_b_gain,
    ap_uint<8> gamma_mode,
    ap_uint<8> denoise_strength,
    ap_uint<8> input_is_bgr
);
```

接口说明：

| 参数 | 说明 |
|---|---|
| `src` | 24-bit RGB/BGR AXI4-Stream 输入 |
| `y_dst` | 8-bit Y 分量 AXI4-Stream 输出 |
| `uv_dst` | 8-bit UV 交错 AXI4-Stream 输出 |
| `rows` / `cols` | 输入帧高、宽 |
| `roi_x/y/w/h` | ROI 左上角和宽高 |
| `wb_r/g/b_gain` | Q8 白平衡增益，256 表示 1.0x |
| `gamma_mode` | 0 bypass，1 bright，2 dark，3 dark strong |
| `denoise_strength` | 降噪强度，0 表示关闭 |
| `input_is_bgr` | 1 表示输入为 BGR，0 表示输入为 RGB |

## 构建

加载 Vitis 环境：

```bash
source /opt/Xilinx/2025.2.1/Vitis/settings64.sh
```

运行 HLS：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/video_preprocess
vitis-run --mode hls --tcl scripts/run_hls.tcl
```

输出路径：

```text
build/hls_video_preprocess/video_preprocess_prj/
ipcore_file/video_preprocess/
```

`scripts/run_hls.tcl` 默认运行 CSIM、CSYNTH、COSIM，并导出 IP catalog。脚本会自动检测 `OPENCV_ROOT` 或 `CONDA_PREFIX`，找不到 OpenCV 时会以纯 C++ testbench 方式继续。

## Testbench

测试入口：`tb/tb_top.cpp`

覆盖场景：

- 640x480 基础 ROI。
- 奇数分辨率 641x481。
- 小尺寸 8x8。
- Bright/Dark Gamma。
- 白平衡增益。
- 降噪开关。
- BGR 输入。
- 局部 ROI。
- WB + Gamma + Denoise 组合。

判定重点：

- `y_dst` 输出数量应为 `rows * cols`。
- `uv_dst` 输出数量按偶数行列计算：`(even_rows / 2) * even_cols`。
- 首个 Y/UV 包的 `user` 信号应置位。

## 实现要点

- 顶层使用 `#pragma HLS DATAFLOW`。
- `stage_isp_processing` 负责 ROI 判断、白平衡、Gamma 和降噪。
- `stage_yuv_conversion` 负责 BT.601 定点转换和 2x2 色度下采样。
- ROI 外区域当前输出清零，并重置降噪状态。
- 奇数行/列时，UV 只输出完整 2x2 块，边缘色度按 NV12 约定丢弃。

## 已知基线

旧综合记录显示在 `xc7z020clg400-1`、10 ns 目标时钟下估计时钟约 7.268 ns。实际资源和时序以本地重新运行后的 HLS 报告为准。

## 关联文档

- RGB2YUV/NV12 说明：`README_RGB2YUV.md`
- PYNQ-Z2 顶层说明：`../../README.md`
- 顶层项目说明：`../../../../README.md`
