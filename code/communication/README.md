# UDP 双向通信说明（PYNQ <-> PC）

本目录当前阶段只验证 PYNQ 与 PC 之间的 UDP 双向通信是否成功。

## 当前状态

- 已完成 PYNQ 与 PC 双向 UDP 互通验证。
- 已确认在 WSL 环境下可稳定运行（PC 端运行在 WSL）。

## 当前联通关系

- PYNQ 持续向 PC 发送文本消息。
- PC 持续接收 PYNQ 消息并原路回 ACK。
- PYNQ 持续接收 PC 返回的 ACK。

## 文件说明

- [include/udp_common.h](include/udp_common.h) 提供 UDP 公共函数、IP 宏和端口常量。
- [src/client_udp.cpp](src/client_udp.cpp) 是 PC 端程序，持续接收并回 ACK。
- [src/pynq_udp.cpp](src/pynq_udp.cpp) 是 PYNQ 端程序，持续发送并等待 ACK。
- [CMakeLists.txt](CMakeLists.txt) 提供 CMake 构建入口。

## 参数配置

请在 [include/udp_common.h](include/udp_common.h) 中确认以下宏值已经填写正确：

- `PC_IP`
- `PYNQ_IP`
- `PC_PORT`
- `PYNQ_PORT`

说明：

- PC 程序监听 `PC_PORT`。
- PYNQ 程序监听 `PYNQ_PORT`。
- PYNQ 发送目标是 `PC_IP:PC_PORT`。

## 已验证网络参数

- `PC_IP = 192.168.2.100`
- `PYNQ_IP = 192.168.2.99`
- `PC_PORT = 5555`
- `PYNQ_PORT = 9090`

说明：

- PYNQ 地址固定为 `192.168.2.99`。
- PC 与 PYNQ 需处于同一网段（`192.168.2.x`）。

## 构建步骤（CMake）

在通信目录下执行：

```bash
cmake -S . -B build
cmake --build build -j
```

生成目标：

- `build/pc_udp`
- `build/pynq_udp`

## 运行顺序

1. 先在 PC 上运行 `./build/pc_udp`。
2. 再在 PYNQ 上运行 `./build/pynq_udp`。
3. 两端持续运行，观察收发日志是否稳定。

## 成功判定

当双向通信成功时，日志应持续出现：

- PYNQ 端打印 `send ok`。
- PC 端打印 `recv` 与 `send ACK ok`。
- PYNQ 端打印 `recv ACK: ACK_FROM_PC`。

补充：

- 手动按 `Ctrl+C` 结束程序时，终端返回 `Exit Code: 130` 属于正常退出现象。

## 验证消息

- PYNQ -> PC: `HELLO_PYNQ_TO_PC seq=<序号>`
- PC -> PYNQ: `ACK_FROM_PC`

## 常见问题

- 若 PYNQ 端持续提示 ACK 超时，请先检查 PC 程序是否已启动并监听正确端口。
- 若 PC 端没有收到消息，请检查 PYNQ 发送目标 IP 和端口是否与头文件一致。
- 若两端同网段仍收不到包，请临时关闭或放行防火墙 UDP 规则后重试。

## 快速回归清单

1. 确认 [include/udp_common.h](include/udp_common.h) 中 IP 与端口与现场一致。
2. 在通信目录执行一次 CMake 全量编译。
3. 先启动 `pc_udp`，再启动 `pynq_udp`。
4. 观察两端日志持续出现收发与 ACK 信息。
5. 连续运行 10 分钟，确认无长时间超时。
