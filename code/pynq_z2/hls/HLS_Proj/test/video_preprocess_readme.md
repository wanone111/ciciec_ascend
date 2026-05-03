# HLS_Proj/test 历史 video_preprocess 工程

本目录是早期 `video_preprocess` 实验工程，保留用于追溯 ROI、轻量 ISP、灰度/阈值模式和 ROI 外区域策略等设计探索。当前推荐维护的主线工程位于：

```text
code/pynq_z2/hls/video_preprocess/
```

## 当前工程定位

该历史版本的顶层接口仍为单路 24-bit RGB 输入和单路 24-bit RGB 输出：

```cpp
void video_preprocess(
    hls::stream<axis_rgb_t> &src,
    hls::stream<axis_rgb_t> &dst,
    ap_uint<16> rows,
    ap_uint<16> cols,
    ap_uint<16> roi_x,
    ap_uint<16> roi_y,
    ap_uint<16> roi_w,
    ap_uint<16> roi_h,
    ap_uint<2>  proc_mode,
    ap_uint<8>  threshold_val,
    ap_uint<16> wb_r_gain,
    ap_uint<16> wb_g_gain,
    ap_uint<16> wb_b_gain,
    ap_uint<2>  gamma_mode,
    ap_uint<8>  denoise_strength,
    ap_uint<2>  roi_bg_mode
);
```

与主线 `hls/video_preprocess` 的区别：

- 历史版本输出仍是 RGB 单流。
- 主线版本输出是 Y/UV 双 8-bit AXI4-Stream，面向 NV12。
- 历史版本包含 `proc_mode`、`threshold_val`、`roi_bg_mode` 等调试参数。
- 主线版本聚焦 ISP + ROI + RGB2YUV/NV12。

## 目录结构

```text
test/
├── include/
│   ├── isp.h
│   ├── roi.h
│   ├── top.h
│   └── video_params.h
├── scripts/
│   └── run_hls.tcl
├── src/
│   ├── isp.cpp
│   ├── roi.cpp
│   └── top.cpp
├── tb/
│   └── tb_top.cpp
└── video_preprocess_readme.md
```

## 运行方式

```bash
source /opt/Xilinx/2025.2.1/Vitis/settings64.sh
cd /home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/HLS_Proj/test
vitis-run --mode hls --tcl scripts/run_hls.tcl
```

如果脚本依赖 OpenCV，请根据本机环境设置 `OPENCV_ROOT` 或 `CONDA_PREFIX`。

## 保留价值

- 作为 ROI 和 ISP 参数接口的历史参考。
- 用于对比单 RGB 输出版本与当前 NV12 双流版本的设计取舍。
- 不建议直接作为当前上板主线使用。
