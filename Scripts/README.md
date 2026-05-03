# Scripts 目录说明

本目录保存项目级辅助脚本，主要用于 Ascend 环境加载、PYNQ HLS 快速启动和后续安装/导出流程占位。

## 当前结构

```text
Scripts/
├── README.md
├── ascend_310b/
│   ├── export_to_onnx.py
│   └── set_env.sh
├── installation/
│   └── setup.sh
└── pynq_z2/
    ├── run_hls.tcl
    └── run.sh
```

## Ascend 310B 环境脚本

`ascend_310b/set_env.sh` 用于加载 CANN 和交叉编译工具链环境：

```bash
source Scripts/ascend_310b/set_env.sh
```

脚本当前会配置：

- `ASCEND_HOME`
- CANN 官方 `set_env.sh`
- `LD_LIBRARY_PATH`
- `TE_PARALLEL_COMPILER=2`
- `aarch64-linux-gnu-` 交叉编译工具链变量
- `check_ascend_env` 检查函数

使用前请确认脚本中的本机路径是否符合当前环境，例如：

```text
/home/bb/Ascend/ascend-toolkit
/home/bb/competition_ws/toolchain/toolchain/bin
```

`ascend_310b/export_to_onnx.py` 当前为空，占位用于后续模型导出脚本。

## PYNQ-Z2 HLS 脚本

`pynq_z2/run.sh` 是一个极简启动脚本：

```bash
source /opt/Xilinx/2025.2.1/Vitis/settings64.sh
vitis-run --mode hls --tcl run_hls.tcl
```

更推荐直接使用当前主工程中的脚本：

```bash
cd code/pynq_z2/hls/video_preprocess
vitis-run --mode hls --tcl scripts/run_hls.tcl
```

`pynq_z2/run_hls.tcl` 是旧实验脚本，内容指向 Vitis Libraries 的 kalmanfilter 示例，不是当前 `video_preprocess` 主线工程。

## 安装脚本

`installation/setup.sh` 当前为空，占位用于后续统一安装依赖或初始化环境。

## 维护建议

- 新增脚本时在本 README 中补充用途、运行目录和依赖。
- 需要 `source` 的脚本应明确说明，避免用户用 `bash script.sh` 导致环境变量不生效。
- 涉及本机绝对路径的脚本，优先改为环境变量或自动探测。
