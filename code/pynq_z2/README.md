# PYNQ-Z2 侧实现说明

本目录用于承接赛题中的 PYNQ 侧工作，目标是完成前端视频流预处理并与 310B 协同。

## 1. 目录与状态（基于当前仓库）

- HLS 主工程目录: [hls/HLS_Proj/ROI_Test](hls/HLS_Proj/ROI_Test)
- HLS 旧测试目录: [hls/HLS_Proj/test](hls/HLS_Proj/test)
- HLS 临时目录/压缩包: [hls/HLS_Proj_Temp](hls/HLS_Proj_Temp), [hls/HLS_Proj_Temp_roi_yuv420_roi_rgbout.zip](hls/HLS_Proj_Temp_roi_yuv420_roi_rgbout.zip)
- PS 脚本目录: [ps](ps)
- PL 比特流说明: [pl/README.md](pl/README.md)

当前可综合入口文件：

- 顶层声明: [hls/HLS_Proj/ROI_Test/include/top.h](hls/HLS_Proj/ROI_Test/include/top.h)
- 参数定义: [hls/HLS_Proj/ROI_Test/include/video_params.h](hls/HLS_Proj/ROI_Test/include/video_params.h)
- ROI 模块: [hls/HLS_Proj/ROI_Test/include/roi.h](hls/HLS_Proj/ROI_Test/include/roi.h), [hls/HLS_Proj/ROI_Test/src/roi.cpp](hls/HLS_Proj/ROI_Test/src/roi.cpp)
- ISP 模块: [hls/HLS_Proj/ROI_Test/include/isp.h](hls/HLS_Proj/ROI_Test/include/isp.h), [hls/HLS_Proj/ROI_Test/src/isp.cpp](hls/HLS_Proj/ROI_Test/src/isp.cpp)
- 顶层实现: [hls/HLS_Proj/ROI_Test/src/top.cpp](hls/HLS_Proj/ROI_Test/src/top.cpp)
- Testbench: [hls/HLS_Proj/ROI_Test/tb/tb_top.cpp](hls/HLS_Proj/ROI_Test/tb/tb_top.cpp)
- HLS 脚本: [hls/HLS_Proj/ROI_Test/scripts/run_hls.tcl](hls/HLS_Proj/ROI_Test/scripts/run_hls.tcl)

## 2. PYNQ 与 310B 分工

- PYNQ-Z2: 采集适配、轻量预处理、ROI 流控、稳定输出
- 310B: DVPP 几何处理、AIPP 数学预处理、NPU 推理、业务判定

原则：缩放与归一化只在一侧执行一次，避免重复计算与带宽浪费。

## 3. 当前 HLS 内核能力

当前顶层函数为 `video_preprocess`，定义见 [hls/HLS_Proj/ROI_Test/include/top.h](hls/HLS_Proj/ROI_Test/include/top.h)。

数据接口：

- `src`: AXI4-Stream 输入（24-bit RGB）
- `dst`: AXI4-Stream 输出（24-bit RGB）

控制参数（AXI-Lite）：

- 分辨率: `rows`, `cols`
- ROI: `roi_x`, `roi_y`, `roi_w`, `roi_h`
- ISP: `wb_r_gain`, `wb_g_gain`, `wb_b_gain`, `gamma_mode`, `denoise_strength`
- 灰度/阈值: `gray_en`, `threshold_en`, `threshold_val`

实现行为（当前代码）：

- 支持运行时 ROI 控制
- ROI 内执行白平衡、Gamma、简易去噪
- ROI 内可选灰度与二值化
- ROI 外输出清零

ROI 内处理顺序：白平衡 -> Gamma -> 简易降噪 -> 可选灰度/阈值。

编译期最大分辨率限制定义在 [hls/HLS_Proj/ROI_Test/include/video_params.h](hls/HLS_Proj/ROI_Test/include/video_params.h)：`MAX_WIDTH=1280`, `MAX_HEIGHT=720`。

## 4. PS 侧脚本

USB 摄像头到 PL 的桥接相关文件：

- 主脚本: [ps/usb_camera_to_pl.py](ps/usb_camera_to_pl.py)
- Notebook 版本: [ps/usb_camera_to_pl.ipynb](ps/usb_camera_to_pl.ipynb)
- 摄像头预检: [ps/usb_camera.py](ps/usb_camera.py)
- 无 HDMI 流程脚本: [ps/PL_ROI_NoDHMI.py](ps/PL_ROI_NoDHMI.py)

[ps/usb_camera_to_pl.py](ps/usb_camera_to_pl.py) 当前支持：

- USB 摄像头采集（OpenCV）
- BGR 帧打包为 32-bit RGB（用于 DMA/VDMA）
- 向 `video_preprocess` IP 写控制寄存器
- dry-run 模式（无板卡先验证流程）
- 每秒输出 FPS 与桥接时延统计

## 5. 运行方式

HLS 一键流程：

```bash
cd /home/bb/competition_ws/pynq_z2
vitis-run --mode hls --tcl hls/HLS_Proj/ROI_Test/scripts/run_hls.tcl
```

说明：`run_hls.tcl` 会在 [hls/HLS_Proj/ROI_Test/build/hls_video_preprocess](hls/HLS_Proj/ROI_Test/build/hls_video_preprocess) 生成工程和综合结果。

PS 侧 dry-run 示例：

```bash
cd /home/bb/competition_ws/pynq_z2
python3 ps/usb_camera_to_pl.py --dry-run --camera 0 --width 640 --height 480 --show
```

摄像头能力探测示例：

```bash
cd /home/bb/competition_ws/pynq_z2
python3 ps/usb_camera_to_pl.py --probe --camera 0
```

上板运行示例（按实际 bit/hwh 与 IP 名称调整）：

```bash
cd /home/bb/competition_ws/pynq_z2
python3 ps/usb_camera_to_pl.py --overlay <overlay.bit> --ip-name video_preprocess --camera 0 --width 640 --height 480 --roi 0.1,0.1,0.8,0.8
```

## 6. 联调建议

- 先跑 HLS testbench，再上板联调。
- 先 dry-run，再切换 overlay + DMA 实链路。
- bit/hwh 文件配对规则参考 [pl/README.md](pl/README.md)。

## 7. 参考

- AMD Vitis Libraries: [https://github.com/Xilinx/Vitis_Libraries](https://github.com/Xilinx/Vitis_Libraries)
