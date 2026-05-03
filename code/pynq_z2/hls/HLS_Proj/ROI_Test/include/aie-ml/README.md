# aie-ml 头文件快照

本目录位于历史 `ROI_Test` 工程下，保存 AIE-ML/Vitis Vision 相关头文件快照，供早期 ROI/HLS 实验工程编译引用。

当前主线工程不依赖此目录；推荐使用：

```text
code/pynq_z2/hls/video_preprocess/
```

维护建议：

- 不在这里直接开发新功能。
- 如果需要升级 AIE-ML/Vitis Vision 头文件，优先从官方库同步并记录版本来源。
- 当前智能视觉交互演示链路主要使用 PYNQ-Z2 HLS 主工程、`code/communication` 和 `smart_ip_interaction`。
