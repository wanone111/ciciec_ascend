# 昇腾 310B 代码

定位：系统"语义推理与决策中枢"。剥离所有像素级软计算，专注张量推理、目标追踪、状态机判定与协议分发。

## 当前状态

- ✅ **姿态识别模型集成**: `yolo26/yolo26s-pose.om` 已部署
- ✅ **动作分类逻辑**: 挥手/欢呼等动作的状态机判定
- ✅ **三端 UDP 通信**: 推理结果通过 UDP 发送给 PC 端
- ✅ **工程可编译**: 生成 `ascend310b_pose` 可执行文件
- 🔴 **表情捕捉模块**: 待实现（人脸检测、DVPP ROI 处理、表情分类网络）
- 🔴 **IP 视频联动控制**: 待实现（视频资产管理、触发引擎、播放控制）
- 🔴 **端到端数据通路**: 待联调（推理→视频触发闭环）

## 核心功能

### 已完成

1. **AIPP/DVPP 硬件预处理**: 支持静态 AIPP 配置 (`config/aipp_static.cfg`)
2. **NPU 姿态推理**: 基于 YOLOv26s-Pose 模型进行骨骼关键点提取
3. **动作状态机**: 根据关键点判定挥手、欢呼等交互动作
4. **协议分发**: 通过 UDP 将推理结果 (JSON 格式) 发送至 PC 端

### 待完成

1. **表情捕捉链路**
   - 人脸检测算法集成
   - DVPP ROI 区域裁剪
   - 表情分类网络推理

2. **IP 视频联动** (采用 Coze 预生成视频方案)
   - 视频资产管理模块 (`ip_player/`)
   - 触发逻辑引擎（根据动作/表情匹配视频）
   - 无缝切换播放控制

3. **性能优化**
   - 端到端时延 < 100ms
   - 动作识别准确率 > 90%
   - 光照变化鲁棒性测试

## 目录结构

```
ascend_310b/
├── CMakeLists.txt          # 构建配置
├── README.md               # 本文档
├── config/
│   └── aipp_static.cfg     # AIPP 静态配置文件
├── include/
│   └── pose_protocol.h     # 姿态推理协议定义
├── scripts/
│   └── build.sh            # 编译脚本
├── src/
│   └── main.cpp            # 主程序入口（推理 + 状态机+UDP 发送）
├── toolchain/
│   └── aarch64.cmake       # 交叉编译工具链
├── yolo26/
│   ├── yolo26s-pose.om     # ✅ 已部署的姿态识别模型
│   └── yolo26s-pose.pt     # 原始 PyTorch 模型
└── [待创建]
    └── ip_player/          # 🔴 IP 视频播放控制模块
```

## 构建步骤

```bash
# 进入目录
cd code/ascend_310b

# 执行构建脚本
./scripts/build.sh

# 或手动构建
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=toolchain/aarch64.cmake
cmake --build build -j
```

生成目标：`build/ascend310b_pose`

## 运行说明

```bash
# 启动 310B 推理程序
./build/ascend310b_pose
```

程序将：
1. 初始化 NPU 和 DVPP
2. 加载 `yolo26s-pose.om` 模型
3. 从 UDP 接收视频帧（来自 PYNQ）
4. 执行姿态推理和动作判定
5. 将推理结果通过 UDP 发送至 PC

## 动作识别协议

推理结果通过 UDP 发送至 PC，格式为 JSON：

```json
{
  "timestamp_us": 1234567890,
  "action_flag": 1,
  "confidence": 0.95,
  "keypoints": [...]
}
```

- `action_flag`: 0=无动作，1=挥手，2=欢呼，3=其他
- `confidence`: 置信度 (0.0-1.0)
- `keypoints`: 骨骼关键点坐标数组

## 里程碑状态

| 里程碑 | 任务 | 状态 |
|--------|------|------|
| M1 | FPGA 视频预处理 | ✅ 完成 |
| M2 | 310B 姿态推理 | ✅ 完成 |
| M3 | 三端 UDP 通信 | ✅ 完成 |
| M4 | 表情捕捉模块 | 🔴 待开发 |
| M5 | IP 视频联动 | 🔴 待开发 |
| M6 | 端到端联调 | 🔴 待测试 |

## 已知问题

- 表情捕捉链路尚未集成，当前仅支持姿态识别
- IP 视频播放控制模块待创建
- 端到端性能指标待验证

## 下一步计划

1. 实现人脸检测和表情分类网络
2. 创建 `ip_player/` 模块，集成视频播放控制
3. 完成推理结果到视频触发的闭环逻辑
4. 进行端到端性能测试和优化