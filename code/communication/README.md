# UDP ACK 通信说明

本目录当前只保留最小的双向 ACK 验证链路，不再涉及视频流、OpenCV 或分片协议。

## 当前状态

- 2026-04-20 已完成三端联调，`pc_udp`、`310B_udp`、`pynq_udp` 可同时运行。
- 现网配置下可稳定形成 ACK 闭环：PYNQ -> 310B -> PC -> 310B -> PYNQ。
- 手动停止程序时若出现退出码 `130`，通常是 `Ctrl+C` 中断，属于正常现象。

## 当前链路

- PYNQ 周期性发送文本到 310B。
- 310B 收到后回 ACK 给 PYNQ。
- 310B 同时把固定文本发给 PC。
- PC 收到后回 ACK 给 310B。

## 文件说明

- [include/udp_common.h](include/udp_common.h) 提供 UDP 公共函数、IP 和端口。
- [src/pynq_udp.cpp](src/pynq_udp.cpp) 是 PYNQ 端 ACK 发送程序。
- [src/310B_udp.cpp](src/310B_udp.cpp) 是 310B 中继程序。
- [src/pc_udp.cpp](src/pc_udp.cpp) 是 PC 端 ACK 回复程序。
- [CMakeLists.txt](CMakeLists.txt) 提供 CMake 构建入口。

## 已知地址

- `PC_IP = 192.168.137.1`
- `ASCEND_310B_IP = 192.168.2.2`（310B 的 `eth0`，用于 PYNQ 链路）
- `ASCEND_310B_PC_IP = 192.168.137.100`（310B 的 `eth1`，用于 PC 链路）
- `PYNQ_IP = 192.168.2.99`
- `PC_PORT = 5555`
- `PYNQ_PORT = 9090`

## 构建步骤

在通信目录下执行：

```bash
cmake -S . -B build
cmake --build build -j
```

生成目标：

- `build/pc_udp`
- `build/310B_udp`
- `build/pynq_udp`

## 运行顺序

1. 先启动 `pc_udp`：`./build/pc_udp`
2. 再启动 `310B_udp`：`./build/310B_udp`
3. 最后启动 `pynq_udp`：`./build/pynq_udp`

## 成功判定

- PYNQ 端持续看到 `send ok` 和 `recv ACK`。
- 310B 端持续看到来自 PYNQ 的消息、发往 PC 的消息和两边 ACK。
- PC 端持续看到收到消息并回 ACK。

参考日志关键字：

- PYNQ: `[PYNQ] send ok`、`[PYNQ] recv ACK: ACK_FROM_310B`
- 310B: `[310B] recv from PYNQ`、`[310B] send to PC ok`、`[310B] recv ACK from PC`、`[310B] send ACK to PYNQ ok`
- PC: `[PC] recv: HELLO_FROM_310B`、`[PC] send ACK ok`

## 执行清单

1. 确认 [include/udp_common.h](include/udp_common.h) 中 IP 和端口已经填写。
2. 在 [CMakeLists.txt](CMakeLists.txt) 目录执行 CMake 构建。
3. 依次启动 PC、310B、PYNQ 三个程序。
4. 观察三端日志是否持续形成 ACK 闭环。
5. 如需排错，先看端口是否被占用，再看 IP 是否可达。
