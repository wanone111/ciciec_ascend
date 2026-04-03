
# 删除之前运行产生的日志文件，清理工作环境
exec sh -c "rm -f flex*.log"

# 打印脚本开始执行的时间信息，便于跟踪执行过程
puts "successful!!!  the start time is [clock format [clock seconds]]"

# ============ 自动推导项目根路径 ============
set CUR_DIR [pwd]
set XF_PROJ_ROOT [file normalize "$CUR_DIR/../../.."]

# ============ OpenCV环境配置部分 ============
# 设置OpenCV安装路径（conda环境中的OpenCV）
set opencv_path "/home/bb/miniconda3/envs/310B_env"
# 设置OpenCV头文件路径（包含opencv2等头文件）
set opencv_include "$opencv_path/include/opencv4"

# 设置OpenCV库文件路径（包含.so动态库文件）
set opencv_lib "$opencv_path/lib"
# 定义 Tcl 局部变量
set OPENCV_INCLUDE "$opencv_include"
set OPENCV_LIB "$opencv_lib"

# 设置环境变量LD_LIBRARY_PATH，确保运行时能找到OpenCV动态库
set ::env(OPENCV_INCLUDE) "$opencv_include"
set ::env(OPENCV_LIB) "$opencv_lib"
set ::env(LD_LIBRARY_PATH) "$opencv_lib:$::env(LD_LIBRARY_PATH)"

# ============ HLS项目配置部分 ============
# 打开或创建HLS项目，项目命名
open_project kalmanfilter.prj

# 设置顶层函数名为（即要综合的主函数）
set_top kalmanfilter_accel

# 添加设计源文件cpp到项目中（从src目录）
add_files "${XF_PROJ_ROOT}/L1/examples/kalmanfilter/xf_kalmanfilter_accel.cpp" -cflags " -I ${XF_PROJ_ROOT}/L1/examples/kalmanfilter/config -I${XF_PROJ_ROOT}/L1/include -I ./ -D__SDSVHLS__ -std=c++0x" -csimflags " -I ${XF_PROJ_ROOT}/L1/examples/kalmanfilter/config -I${XF_PROJ_ROOT}/L1/include -I ./ -D__SDSVHLS__ -std=c++0x"

# 添加头文件h到项目中（从src目录）
#add_files ../src//include/

# 添加测试文件testbench.cpp，并设置编译和链接选项：
add_files -tb "${XF_PROJ_ROOT}/L1/examples/kalmanfilter/xf_kalmanfilter_tb.cpp" -cflags " -I ${XF_PROJ_ROOT}/L1/examples/kalmanfilter/config -I${OPENCV_INCLUDE} -I${XF_PROJ_ROOT}/L1/include -I ./ -D__SDSVHLS__ -std=c++0x" -csimflags " -I ${XF_PROJ_ROOT}/L1/examples/kalmanfilter/config -I${XF_PROJ_ROOT}/L1/include -I ./ -D__SDSVHLS__ -std=c++0x"

# 打开解决方案solution1，如果存在则重置（清空之前的结果）
open_solution -reset solution1

#设置目标FPGA器件为pynqz2（xc7z020clg400-1封装）
set_part {xc7z020clg400-1}

#创建时钟约束，设置时钟周期为10ns（即100MHz频率）
create_clock -period 3.3 

# ============ HLS流程执行部分 ============
# 运行C仿真（CSIM），验证算法功能正确性
csim_design -ldflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_video"

# 运行C综合（CSYNTH），将C++代码转换为RTL硬件描述
csynth_design

# 运行C/RTL协同仿真（COSIM），验证综合后RTL与C++行为一致性
cosim_design -ldflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_video"

#导出综合级RTL（仅逻辑综合，不布局布线，用于常规Vivado IP集成）
export_design -flow syn -rtl verilog

#导出实现级RTL（含布局布线，用于精确时序/功耗评估，耗时较长）
export_design -flow impl -rtl verilog

# 退出Vitis HLS工具
exit


