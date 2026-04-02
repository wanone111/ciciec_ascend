# Vitis HLS + Vitis Vision Harris 示例排障总结（Windows → Linux）

## 1. 目标与最终结果

目标是跑通 `vision/L1/examples/harris` 这个 Vitis Vision Library 示例。  
最终在 **Ubuntu/Linux** 环境下成功跑通了 `vitis-run --mode hls --tcl run_hls.tcl` 的 C Simulation（CSIM）。

这次排障经历了两大阶段：

1. **Windows 阶段**：解决 OpenCV 头文件、库文件、MinGW 运行时和 DLL 冲突问题。  
2. **Linux 阶段**：解决 `run_hls.tcl` 的器件配置、OpenCV 头文件路径、旧版头文件引用方式、以及 `GLIBCXX` 链接问题。

---

## 2. 从头到尾的核心问题链路

### 阶段 A：Windows 下最初的问题

最开始执行：

```bash
vitis-run --mode hls --tcl run_hls.tcl
```

最先出现的报错是：

```text
no such variable
(read trace on "::env(OPENCV_INCLUDE)")
```

### 根因 1：没有设置 OpenCV 环境变量

`run_hls.tcl` 里直接读取：

```tcl
set OPENCV_INCLUDE $::env(OPENCV_INCLUDE)
set OPENCV_LIB $::env(OPENCV_LIB)
```

但当时终端里并没有设置这两个变量，所以 Tcl 直接退出。

### 解决方式

在 CMD 中设置：

```cmd
set OPENCV_INCLUDE=C:\OpenCV\opencv-4.13.0\build_win64\install\include
set OPENCV_LIB=C:\OpenCV\opencv-4.13.0\build_win64\install\x64\mingw\lib
set PATH=%PATH%;C:\OpenCV\opencv-4.13.0\build_win64\install\x64\mingw\bin
```

---

## 3. Windows 第二个问题：目标器件不匹配

之后出现：

```text
ERROR: [HLS 200-1023] Part 'xcu200-fsgd2104-2-e' is not installed.
```

### 根因 2：`run_hls.tcl` 默认写死了 U200 板卡器件

脚本原本使用的是：

```tcl
set XPART xcu200-fsgd2104-2-e
```

而本机安装的器件库中并没有 U200，对应实际开发板更接近 `PYNQ-Z2`。

### 解决方式

先查询本机可用 part：

```tcl
get_parts *xc7z*
```

最终改为：

```tcl
set XPART xc7z020clg400-1
```

---

## 4. Windows 第三个问题：OpenCV 头文件找不到

继续运行后出现：

```text
fatal error: 'opencv2/highgui/highgui.hpp' file not found
```

### 根因 3.1：`-csimflags` 中没有带上 OpenCV include 路径

最初 `run_hls.tcl` 只在 `-cflags` 中加入了 OpenCV 路径，没有在 `-csimflags` 里加入，导致 `csim_design` 编译 testbench 时看不到 OpenCV 头文件。

### 解决方式

把 testbench 的 Tcl 配置改成同时包含：

```tcl
-cflags "... -I${OPENCV_INCLUDE} ..."
-csimflags "... -I${OPENCV_INCLUDE} ..."
```

---

### 根因 3.2：OpenCV 安装本身不完整

进一步检查后发现：

- `opencv2/core.hpp` 存在
- `opencv2/imgproc.hpp` 存在
- `opencv2/imgcodecs.hpp` 存在
- 但 `opencv2/highgui.hpp` / `opencv2/highgui/highgui.hpp` 最初不存在

这说明之前那套 OpenCV 编译安装并不完整，至少缺少 `highgui` 模块。

### 解决方式

重新清理并编译 OpenCV，确保构建了这些模块：

- `core`
- `imgproc`
- `imgcodecs`
- `highgui`

Windows 下重建时关键检查命令：

```cmd
dir "C:\OpenCV\opencv-4.13.0\build_win64\install\include\opencv2\highgui.hpp"
dir "C:\OpenCV\opencv-4.13.0\build_win64\install\x64\mingw\lib\libopencv_highgui*"
```

确认存在后，说明 OpenCV 安装完整。

---

## 5. Windows 第四个问题：旧版 OpenCV 头文件写法不兼容

后来即使 OpenCV 装对了，依然报：

```text
fatal error: 'opencv2/video/video.hpp' file not found
```

以及此前也有类似的：

```text
opencv2/highgui/highgui.hpp
opencv2/imgproc/imgproc.hpp
opencv2/imgcodecs/imgcodecs.hpp
```

### 根因 4：`xf_headers.hpp` 使用的是旧版 OpenCV 头文件路径

`xf_headers.hpp` 里原来的写法类似：

```cpp
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgcodecs/imgcodecs.hpp"
#include "opencv2/video/video.hpp"
```

但 OpenCV 4.x 更常用、也更稳定的路径是：

```cpp
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgcodecs.hpp"
```

而 Harris 这个例子本身并不需要 `video` 模块。

### 解决方式

修改 `xf_headers.hpp`：

```cpp
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgcodecs.hpp"
```

并删除：

```cpp
#include "opencv2/video/video.hpp"
```

---

## 6. Windows 第五个问题：链接阶段找不到 OpenCV 库

后续报错变成：

```text
lld: error: unable to find library -lopencv_imgcodecs4130
lld: error: unable to find library -lopencv_imgproc4130
...
```

### 根因 5：Windows 下 clang/lld 没正确解析 `-lopencv_xxx4130`

虽然 `.dll.a` 文件实际存在，但 Vitis HLS 的 clang/lld 环境没有按预期找到它们。

### 解决方式

在 Tcl 中：

- 只保留 Harris 实际需要的库
- 显式传递正确的 OpenCV 版本号后缀
- 后续还尝试过直接使用库完整路径的方式

但 Windows 下最难处理的不是这一步，而是接下来的运行时问题。

---

## 7. Windows 第六个问题：运行时 DLL 冲突

即使链接阶段逐步推进，仍然出现弹窗错误，典型符号是：

```text
__ZSt28__throw_bad_array_new_length
```

### 根因 6：MinGW `libstdc++` 运行时 DLL 冲突

系统中同时存在多份 `libstdc++-6.dll`：

```cmd
where libstdc++-6.dll
```

结果显示：

- 多份 Xilinx 自带 DLL
- `C:\mingw64\bin\libstdc++-6.dll`
- OpenCV bin 目录中的 DLL

这导致 `csim.exe` 运行时可能加载了错误版本的 GNU C++ 运行时。

### 解决方式

把这些文件复制到 `csim.exe` 同目录：

来自 `C:\mingw64\bin`：

```text
libstdc++-6.dll
libgcc_s_seh-1.dll
libwinpthread-1.dll
```

来自 OpenCV bin：

```text
libopencv_core4130.dll
libopencv_imgproc4130.dll
libopencv_imgcodecs4130.dll
libopencv_highgui4130.dll
```

并通过下面命令定位：

```cmd
dir /s /b csim.exe
```

最终可以手动执行：

```cmd
cd /d C:\Users\32035\Desktop\vision\L1\examples\harris\harris.prj\sol1\csim\build
csim.exe C:\Users\32035\Desktop\vision\data\128x128.png
```

这说明 Windows 下已经把运行时问题逼到了可执行阶段，但整体依然较复杂，因此后续转向 Linux。

---

## 8. 切换到 Linux 的原因

Windows 阶段虽然已经解决了：

- 环境变量
- 目标器件
- OpenCV 安装完整性
- 头文件旧路径
- 大部分链接与 DLL 问题

但 Windows 下的：

- clang/lld + MinGW `.dll.a`
- 多份 `libstdc++-6.dll`
- GUI/运行时依赖

使整条链路过于脆弱。  
因此最终转到 Ubuntu/Linux 环境继续。

---

## 9. Linux 阶段：第一类问题

Linux 下最初依然报：

```text
no such variable
(read trace on "::env(OPENCV_INCLUDE)")
```

### 根因 7：Linux 终端中没有设置 OpenCV 环境变量

### 解决方式

在同一个终端中执行：

```bash
source /home/organe/Xilinx/2025.2.1/settings64.sh
export OPENCV_INCLUDE=/usr/local/include/opencv4
export OPENCV_LIB=/usr/local/lib
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

---

## 10. Linux 第二类问题：默认器件仍然是 U200

报错：

```text
ERROR: [HLS 200-1023] Part 'xcu200-fsgd2104-2-e' is not installed.
```

### 根因 8：Linux 下的 `run_hls.tcl` 仍然是示例默认器件

### 解决方式

把：

```tcl
set XPART xcu200-fsgd2104-2-e
```

改成：

```tcl
set XPART xc7z020clg400-1
```

---

## 11. Linux 第三类问题：`xf_headers.hpp` 仍有旧版头文件路径

Linux 下继续报：

```text
fatal error: 'opencv2/video/video.hpp' file not found
```

### 根因 9：和 Windows 一样，公共头文件还是旧写法

### 解决方式

把 `/home/organe/桌面/vision/L1/include/common/xf_headers.hpp` 修改为：

```cpp
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgcodecs.hpp"
```

删除：

```cpp
#include "opencv2/video/video.hpp"
```

---

## 12. Linux 第四类问题：`GLIBCXX_3.4.29` 链接错误

最关键的 Linux 阶段报错是：

```text
ld.lld: error: undefined reference due to --no-allow-shlib-undefined:
std::__throw_bad_array_new_length()@GLIBCXX_3.4.29
>>> referenced by /usr/local/lib/libopencv_imgproc.so
```

### 根因 10：Vitis HLS 使用的 clang/lld 在链接阶段过于严格

这里不是系统 GCC 太老。后续确认：

```bash
g++ --version
strings "$(readlink -f "$(g++ -print-file-name=libstdc++.so)")" | grep GLIBCXX_3.4.29
```

结果表明：

- 系统 `g++` 是 11.4.0
- 系统 `libstdc++.so` 确实包含 `GLIBCXX_3.4.29`

所以问题不在系统，而在于：

- `ld.lld` 在链接 `csim.exe` 时，默认对共享库中的未当场解析符号报错
- OpenCV `.so` 中的 GNU C++ 符号本来可以留到运行时动态加载阶段再解析

### 最终解决方式

在 `run_hls.tcl` 中，使用更宽松、更适合当前场景的链接参数：

```tcl
set OPENCV_LDFLAGS "-Wl,--allow-shlib-undefined -Wl,-rpath,${OPENCV_LIB} -L ${OPENCV_LIB} -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lopencv_highgui -lstdc++ -lpthread -ldl -lm"
```

并且：

- 保留 `-std=c++17`
- 不再把会破坏标准库头搜索路径的 `--gcc-toolchain` / `-stdlib=libstdc++` 硬塞到编译参数里

这一步之后，Linux 下终于跑通。

---

## 13. 最终跑通时的关键文件改动

### 13.1 `run_hls.tcl`

必要修改包括：

1. 目标器件：

```tcl
set XPART xc7z020clg400-1
```

2. 增加 Linux 默认 OpenCV 路径回退：

```tcl
if {[info exists ::env(OPENCV_INCLUDE)]} {
    set OPENCV_INCLUDE $::env(OPENCV_INCLUDE)
} else {
    set OPENCV_INCLUDE "/usr/local/include/opencv4"
}

if {[info exists ::env(OPENCV_LIB)]} {
    set OPENCV_LIB $::env(OPENCV_LIB)
} else {
    set OPENCV_LIB "/usr/local/lib"
}
```

3. testbench 的 `-csimflags` 中必须带 OpenCV include：

```tcl
-I${OPENCV_INCLUDE}
```

4. 编译标准改成：

```tcl
-std=c++17
```

5. 链接参数最终采用：

```tcl
-Wl,--allow-shlib-undefined
-Wl,-rpath,${OPENCV_LIB}
-L ${OPENCV_LIB}
-lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lopencv_highgui
-lstdc++ -lpthread -ldl -lm
```

---

### 13.2 `xf_headers.hpp`

最终可用写法为：

```cpp
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgcodecs.hpp"
```

删除：

```cpp
#include "opencv2/video/video.hpp"
```

---

## 14. 最终跑通时用到的必要命令

### 14.1 Linux 下加载 Vitis 环境

```bash
source /home/organe/Xilinx/2025.2.1/settings64.sh
```

### 14.2 设置 OpenCV 环境变量

```bash
export OPENCV_INCLUDE=/usr/local/include/opencv4
export OPENCV_LIB=/usr/local/lib
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

### 14.3 进入 Harris 示例目录

```bash
cd ~/桌面/vision/L1/examples/harris
```

### 14.4 删除旧工程缓存

```bash
rm -rf harris.prj
```

### 14.5 运行 HLS

```bash
vitis-run --mode hls --tcl run_hls.tcl
```

### 14.6 检查系统 GCC 版本和 `GLIBCXX_3.4.29`

```bash
g++ --version
strings "$(readlink -f "$(g++ -print-file-name=libstdc++.so)")" | grep GLIBCXX_3.4.29
```

### 14.7 检查 OpenCV 是否安装完整

```bash
ls /usr/local/include/opencv4/opencv2/core.hpp
ls /usr/local/include/opencv4/opencv2/highgui.hpp
ls /usr/local/lib | grep opencv_core
ls /usr/local/lib | grep opencv_highgui
```

---

## 15. 经验总结

### 经验 1：示例自带 Tcl 往往不是你的板卡环境

很多 Vitis Vision 示例默认是面向 Alveo/U200 这类平台的，直接跑之前必须先检查：

- `set_part`
- OpenCV 路径变量
- testbench 编译选项

### 经验 2：OpenCV 问题要区分三个层面

1. **头文件层面**：`opencv2/*.hpp` 找不到  
2. **链接层面**：`-lopencv_*` 找不到  
3. **运行时层面**：`libstdc++` / `dll` / `so` 冲突

这三类问题看起来都像“OpenCV 不对”，但实际根因完全不同。

### 经验 3：Windows 下 Vitis HLS + MinGW OpenCV 的兼容性很脆弱

尤其容易卡在：

- `.dll.a` 导入库搜索
- `libstdc++-6.dll` 冲突
- OpenCV 的运行时依赖

Linux 下整体更容易跑通。

### 经验 4：Vitis HLS 的 `csim_design` 既会编译也会链接

所以只要出现：

- `fatal error: ... file not found`  
说明还是编译阶段问题。

- `undefined reference ...`  
说明已经进入链接阶段。

- 程序启动弹窗或运行时崩溃  
说明已经到了运行时阶段。

顺着这条链路定位会快很多。

---

## 16. 最终可复现的 Linux 操作流程

每次开新终端后：

```bash
source /home/organe/Xilinx/2025.2.1/settings64.sh
export OPENCV_INCLUDE=/usr/local/include/opencv4
export OPENCV_LIB=/usr/local/lib
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
cd ~/桌面/vision/L1/examples/harris
rm -rf harris.prj
vitis-run --mode hls --tcl run_hls.tcl
```

如果 `run_hls.tcl` 和 `xf_headers.hpp` 都已经改好，那么这套命令就可以直接复现当前跑通的状态。

---

## 17. 结论

这次问题不是单一错误，而是一条连续的工程链路问题：

1. Tcl 环境变量未设置  
2. 默认器件与实际板卡不匹配  
3. OpenCV 安装不完整  
4. 公共头文件仍使用旧版 OpenCV 路径  
5. Windows 下 MinGW/OpenCV/Vitis 运行时冲突  
6. Linux 下 lld 对共享库符号解析过严  

最终通过：

- 修正 `run_hls.tcl`
- 修正 `xf_headers.hpp`
- 正确设置 OpenCV 环境变量
- 在 Linux 下采用更合适的链接策略

成功跑通了 `vision/L1/examples/harris`。

