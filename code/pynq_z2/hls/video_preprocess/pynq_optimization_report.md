# PYNQ-Z2 视频预处理 HLS IP 代码优化报告

## 1. 优化对象

本次优化对象为 PYNQ-Z2 端 HLS 视频预处理 IP，主要涉及：

- `src/top.cpp`
- `include/rgb2yuv.h`

该 IP 的功能链路为：AXI4-Stream RGB/BGR 输入 → ISP 处理 → ROI 门控 → RGB 转 YUV420SP/NV12 → Y 流与 UV 流输出。

本次没有修改 RGB/BGR 通道映射，仅优化 HLS 代码结构、流水线位置、NV12 色度下采样逻辑和数据流稳定性。

## 2. 优化背景

原始代码已经具备基本功能，但存在以下不利于 HLS 综合和系统联调的问题：

1. `stage_isp_processing()` 中使用 `i / cols` 和 `i % cols` 计算行列坐标，变量除法和变量取模会带来较高硬件资源消耗。
2. 部分函数同时使用 `INLINE` 与 `PIPELINE`，容易产生 HLS pragma 冲突或综合不稳定。
3. `stage_yuv_conversion()` 的 2×2 UV 平均缓存逻辑存在风险，可能取错当前 2×2 块的左上角像素。
4. UV 流 `TLAST` 判断条件存在问题，原代码使用 `c == even_cols - 2`，而最后一个有效奇数列应为 `even_cols - 1`。
5. 顶层没有统一裁剪 `rows/cols` 后再传入两个 dataflow stage，极端情况下可能导致生产者和消费者处理的像素数量不一致。
6. 存在未实际使用的 `last_uv_r/g/b` 变量。

## 3. 具体优化内容

### 3.1 去除变量除法和变量取模

原始 `stage_isp_processing()` 使用：

```cpp
ap_uint<16> r = i / cols;
ap_uint<16> c = i % cols;
```

优化后改为行列计数器：

```cpp
ap_uint<16> r = 0;
ap_uint<16> c = 0;

if (c == cols - 1) {
    c = 0;
    r++;
} else {
    c++;
}
```

这样可以把变量除法和变量取模替换为加法器、比较器和选择器，更适合 FPGA 硬件实现，有利于降低 LUT 消耗和改善时序。

### 3.2 将 PIPELINE 下沉到像素循环

原代码在函数体开头使用：

```cpp
#pragma HLS PIPELINE II=1
```

优化后将流水线约束放到真正逐像素处理的循环内部：

```cpp
for (...) {
#pragma HLS PIPELINE II=1
    ...
}
```

这样 HLS 工具更容易明确流水线边界，也更符合图像流逐像素处理的结构。

### 3.3 去除小函数内部的 PIPELINE pragma

`calc_yuv_parallel()` 是内联小函数，原来同时写了：

```cpp
#pragma HLS INLINE
#pragma HLS PIPELINE II=1
```

优化后保留 `INLINE`，删除内部 `PIPELINE`。流水线由外层像素循环统一控制，避免同一逻辑被多层流水线约束导致 warning 或调度不稳定。

### 3.4 修正 NV12 2×2 色度下采样缓存逻辑

原始代码使用 `top_left_r/g/b`，并在奇数列之后更新，容易导致当前 2×2 块使用到上一块的像素。

优化后改为：

- 在偶数列保存上一行偶数列像素：`top_even_r/g/b`
- 在偶数列保存当前行偶数列像素：`left_r/g/b`
- 在奇数列到来时，与当前像素组成完整 2×2 块

对应 2×2 平均为：

```cpp
avg = (top_even + top_current + left_current + current + 2) >> 2;
```

这样能更准确地产生 NV12 UV 分量。

### 3.5 修正 UV TLAST 判断

原代码：

```cpp
v_pkt.last = (c == even_cols - 2) ? 1 : 0;
```

由于 UV 只在奇数列输出，而 `even_cols - 2` 是偶数列，因此该条件基本不会在有效输出时触发。

优化后：

```cpp
v_pkt.last = (c == even_cols - 1) ? 1 : 0;
```

这样每条 UV 行的最后一个 V 包能够正确产生 `TLAST`。

### 3.6 顶层统一裁剪 rows/cols

新增：

```cpp
ap_uint<16> proc_rows = clamp_rows(rows);
ap_uint<16> proc_cols = clamp_cols(cols);
```

并且两个 stage 都使用 `proc_rows/proc_cols`。

这样可以保证 dataflow 中前后两个 stage 处理相同数量的像素，避免 FIFO 数据残留或读写数量不匹配。

### 3.7 删除无用变量

删除 `last_uv_r/g/b` 及其赋值。该变量原代码中只写不读，对功能没有贡献，删除后可以减少无意义逻辑和 warning 风险。

## 4. 未修改内容

本次没有修改：

1. RGB/BGR 通道映射。
2. 白平衡、Gamma、去噪算法本身。
3. AXI4-Stream 顶层接口。
4. AXI-Lite 控制寄存器接口。
5. NV12 的 U/V 输出顺序。
6. ROI 策略。当前仍然是“全图输出，ROI 外清零”，不是裁剪成小图输出。

## 5. 预期收益

| 优化项 | 预期收益 |
|---|---|
| 去除 `i / cols` 和 `i % cols` | 降低 LUT 消耗，改善时序路径 |
| PIPELINE 下沉到像素循环 | 提高 HLS 调度稳定性 |
| 删除小函数内部 PIPELINE | 减少 pragma 冲突 warning |
| 修正 2×2 UV 缓存 | 提高 NV12 色度输出正确性 |
| 修正 UV TLAST | 提高 AXI4-Stream 行边界正确性 |
| 统一 rows/cols 裁剪 | 避免 dataflow stage 像素数不一致 |
| 删除无用变量 | 降低无意义逻辑与 warning 风险 |

## 6. 建议验证流程

替换文件后，建议依次执行：

```bash
cd hls/video_preprocess
source /opt/Xilinx/2025.2.1/Vitis/settings64.sh
vitis-run --mode hls --tcl scripts/run_hls.tcl
```

重点检查：

1. CSim 是否通过。
2. C Synthesis 是否通过。
3. II 是否保持为 1。
4. LUT、FF、BRAM、DSP 是否变化。
5. UV 流每条 UV 行的 TLAST 是否正确。
6. Y 数据量是否为 `W × H`。
7. UV 数据量是否为 `W × H / 2`。
8. 输出是否仍为 NV12：Y 全分辨率，UV 按 `U,V,U,V...` 交织。

## 7. 可以写进比赛报告的总结

本次针对 PYNQ-Z2 端视频预处理 HLS IP 进行了面向硬件综合的代码结构优化。通过将行列计算由变量除法/取模改为计数器递增，降低了像素流水线中的组合逻辑复杂度；通过将 `PIPELINE` 约束下沉到逐像素循环并删除内联小函数中的重复流水线约束，减少了 HLS 调度冲突；通过修正 NV12 2×2 色度下采样缓存和 UV 流 TLAST 判断，提高了输出数据契约的正确性；通过在顶层统一裁剪帧尺寸，保证了 dataflow 各 stage 的数据量一致性。该优化保持了原有接口和功能框架不变，适合继续接入后续 Vivado 集成、板级验证和 310B 端联调。
