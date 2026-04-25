# RGB2YUV 说明

## 功能

该模块负责把输入的 RGB/BGR 视频流转换为 YUV420SP（NV12）格式，并通过双 AXI4-Stream 输出：

- Y 流：逐像素输出亮度分量
- UV 流：按 2x2 块下采样后交织输出 U、V 分量

## 输入输出

- 输入：24-bit RGB/BGR AXI4-Stream
- 输出：8-bit Y 流 + 8-bit UV 交织流

## 颜色空间转换

采用 BT.601 的 Q8.8 定点系数：

- Y = 0.299R + 0.587G + 0.114B
- U = -0.169R - 0.331G + 0.500B + 128
- V = 0.500R - 0.419G - 0.081B + 128

实现中使用定点累加后右移 8 位，并进行 0 到 255 的饱和裁剪。

## NV12 约定

- Y 分量分辨率与输入图像一致
- UV 分量按 2x2 块下采样
- U 和 V 交织输出：U0, V0, U1, V1, ...
- 仅输出完整 2x2 块，边缘像素丢弃
- UV 流的 TLAST 按每条 UV 行结束标记

## 相关实现

- 主核实现： [src/top.cpp](src/top.cpp)
- 头文件实现： [include/rgb2yuv.h](include/rgb2yuv.h)
- 顶层接口： [include/top.h](include/top.h)

## 适用场景

适合将视频流整理成 Ascend 310B AIPP 需要的 NV12 输入格式，也适合后续接 AXI DMA 或 UDP 封装模块。
