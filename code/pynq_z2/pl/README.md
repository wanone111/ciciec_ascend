# PL 比特流文件说明

本目录保存 PYNQ-Z2 PL 侧可加载的 bitstream 和硬件描述文件。

## 当前文件

```text
pl/
├── README.md
└── bit/
    ├── hdmi_vdma_bd_v2.bit
    └── hdmi_vdma_bd_v2.hwh
```

PYNQ Overlay 加载时 `.bit` 和 `.hwh` 需要放在同一目录，并保持相同文件名前缀。例如当前这组文件可作为：

```text
hdmi_vdma_bd_v2.bit
hdmi_vdma_bd_v2.hwh
```

## 使用提示

- 将 `bit/` 目录中的 `.bit` 和 `.hwh` 一起拷贝到 PYNQ 板端。
- Python 侧加载 Overlay 时使用 `.bit` 路径，PYNQ 会按同名前缀查找 `.hwh`。
- 如果重新从 HLS/Vivado 导出硬件，务必同步更新这两个文件，避免寄存器映射和实际 bitstream 不一致。

## 关联工程

- 当前 HLS 主工程：`../hls/video_preprocess/`
- PYNQ-Z2 约束文件：`../vivado/xdc/`
