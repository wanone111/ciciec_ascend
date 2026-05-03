# RGB2YUV / NV12 转换说明

本文件说明 `video_preprocess` 中 RGB/BGR 到 YUV420SP/NV12 的转换逻辑。相关实现主要位于：

- `include/rgb2yuv.h`
- `src/top.cpp` 中的 `stage_yuv_conversion`

## 输入输出

输入：

- 24-bit RGB/BGR 像素流。
- 每个像素经过前级 ISP/ROI 后进入转换阶段。

输出：

- `y_dst`：8-bit Y 亮度流，逐像素输出。
- `uv_dst`：8-bit UV 交错流，按完整 2x2 块输出 U、V。

## 色彩转换

采用 BT.601 系数的定点近似：

```text
Y =  0.299R + 0.587G + 0.114B
U = -0.169R - 0.331G + 0.500B + 128
V =  0.500R - 0.419G - 0.081B + 128
```

实现中使用整数系数计算并做 0 到 255 的饱和裁剪。

## NV12 输出约定

NV12 由全分辨率 Y 平面和半分辨率 UV 平面组成：

```text
Y:  rows * cols
UV: rows/2 * cols, U/V 交错
```

在本 IP 中：

- Y 流每个输入像素输出 1 个 Y。
- UV 流每个完整 2x2 像素块输出 1 个 U 和 1 个 V。
- 如果输入存在奇数行或奇数列，UV 阶段只使用向下取偶后的完整 2x2 区域。
- `uv_dst.user` 在第一个 UV 包置位。
- `uv_dst.last` 在每条 UV 行末尾的 V 包置位。

## 与 ROI 的关系

ROI 外像素在前级 `stage_isp_processing` 中被清零，因此 RGB2YUV 阶段不再单独判断 ROI。这样可以保持转换阶段逻辑稳定，代价是 ROI 外区域会输出黑色对应的 Y/UV。

## 测试关注点

`tb/tb_top.cpp` 会检查：

- Y 包数量等于 `rows * cols`。
- UV 包数量等于 `(even_rows / 2) * even_cols`。
- 奇数分辨率下不会输出不完整色度块。
- Y/UV 首包 `user` 信号符合帧起始约定。

## 适用场景

该转换适合将 PYNQ-Z2 前端视频流整理为 Ascend 310B/DVPP/AIPP 友好的 NV12 数据形式，也适合后续接 AXI DMA 或 UDP 封装模块。
