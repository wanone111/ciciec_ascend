# PYNQ-Z2 HLS 架构

本目录用于维护赛题的本地 HLS 工程骨架，目标是先打通可综合/可仿真的最小链路，再逐步替换为更复杂的图像算子流水线。

## 1. 目录结构

- `include/video_params.h`：统一参数定义（分辨率上限、接口位宽、类型、默认阈值）。
- `include/top.h`：顶层函数声明与 Vitis Vision 头文件聚合。
- `src/top.cpp`：顶层核函数 `vision_preprocess_accel`，当前实现 ROI 对齐裁剪 + `BGR -> Gray -> Threshold`。
- `tb/top_tb.cpp`：纯 C testbench（无 OpenCV 依赖），用于 CSIM 快速验功能。
- `scripts/run_hls.tcl`：本地工程 HLS 流程脚本。

## 2. 当前顶层接口

`vision_preprocess_accel` 使用 `m_axi + s_axilite` 接口：

1. 输入：`img_in`，BGR24 打包到 32bit。
2. 输出：`img_out`，8bit 二值图。
3. 参数：`rows`、`cols`、`thresh_val`。

## 3. 当前实现方式

当前可运行版本使用显式像素级 ROI 拷贝 + 整数灰度/阈值处理，优点是：

1. CSIM 行为稳定，便于先打通架构。
2. ROI 对齐规则清晰，方便和通信协议保持一致。
3. 后续可再替换回 Vitis Vision 算子链，不影响上层接口。

后续如果要回到官方算子库，建议在保持 `vision_preprocess_accel` 接口不变的前提下，只替换核函数内部实现。

## 4. 运行方式

在 `scripts` 目录执行（建议）：

```bash
cd /home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/scripts
vitis-run --mode hls --tcl run_hls.tcl
```

说明：

1. 脚本默认执行 `csim + csynth + export syn`。
2. `RUN_COSIM` 默认关闭，需要时可在 TCL 内改为 `1`。
3. 依赖 `vision` 仓库在 `/home/bb/competition_ws/vision`，主要是为了后续扩展到 Vitis Vision 算子时直接复用头文件和工程结构。

## 5. 下一步建议

1. 在 `top.cpp` 中把 ROI 参数继续扩展到控制通道，实现运行时可调。
2. 将输出从 `bin8` 升级到后续视频链路要求的格式（如 `YUV420`）。
3. 增加多组 testbench 用例覆盖边界场景（最小尺寸、奇偶尺寸、极值阈值）。
