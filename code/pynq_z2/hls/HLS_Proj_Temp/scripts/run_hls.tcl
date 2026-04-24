# =============================================================================
# File: /home/bb/competition_ws/pynq_z2/hls/scripts/run_hls.tcl
# Purpose: Vitis HLS 一键流水线脚本 - 视频预处理 (PYNQ-Z2 + Vitis_Libraries)
# Target: xc7z020clg400-1, Clock: 100MHz, Flow: CSIM→CSYNTH→COSIM→IP Export
# Author: Competition Team | Date: 2026
# =============================================================================

# 删除之前运行产生的日志文件，清理工作环境
exec sh -c "rm -f flex*.log"

# 打印脚本开始执行的时间信息，便于跟踪执行过程
puts "successful!!!  the start time is [clock format [clock seconds]]"

# ============ 自动推导项目根路径 ============
set script_dir [file dirname [file normalize [info script]]]
set project_root [file normalize "$script_dir/.."]
set src_dir "$project_root/src"
set inc_dir "$project_root/include"
set tb_dir "$project_root/tb"
set vitis_libs "$project_root/vitis_libs"

# ============ 构建输出路径隔离  ============
set build_root "$project_root/build"
set build_name "hls_video_preprocess"
set build_output "$build_root/$build_name"
file mkdir $build_root
file mkdir $build_output
puts "Build output redirected to: $build_output"

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
open_project -reset "$build_output/video_preprocess_prj"

# 设置顶层函数名为（即要综合的主函数）
set top_name video_preprocess
set_top $top_name

# 添加设计源文件cpp到项目中（从src目录）

set common_cflags "-I $inc_dir -I ./ -D__SDSVHLS__ -DXF_USE_AXI4=1 -std=c++17"

add_files "$src_dir/top.cpp" -cflags "$common_cflags"
add_files "$src_dir/roi.cpp" -cflags "$common_cflags"
# 添加头文件h到项目中（从src目录）
#add_files ../src//include/

# 添加测试文件testbench.cpp，并设置编译和链接选项：
add_files -tb "$tb_dir/tb_top.cpp" -cflags "$common_cflags -I $opencv_include" \
    -csimflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs"

# 打开解决方案solution1，如果存在则重置（清空之前的结果）
open_solution -reset solution1

#设置目标FPGA器件为pynqz2（xc7z020clg400-1封装）
set_part {xc7z020clg400-1}

#创建时钟约束，设置时钟周期为10ns（即100MHz频率）
create_clock -period 10

# ============ HLS流程执行部分 ============
set run_csim 1
set run_cosim 1
set run_export 0

# 运行C仿真（CSIM），验证算法功能正确性
if {$run_csim} {
    csim_design -ldflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_video"
}

# 运行C综合（CSYNTH），将C++代码转换为RTL硬件描述
csynth_design

# 运行C/RTL协同仿真（COSIM），验证综合后RTL与C++行为一致性
if {$run_cosim} {
    cosim_design -ldflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_video"
}

#导出综合级RTL（仅逻辑综合，不布局布线，用于常规Vivado IP集成）
if {$run_export} {
    export_design -flow syn -rtl verilog
}

#导出实现级RTL（含布局布线，用于精确时序/功耗评估，耗时较长）
if {$run_export} {
    export_design -flow impl -rtl verilog
}


# =============================================================================
# 导出 IP 核，便于在后续 Vivado/Vitis 工程中复用
# =============================================================================

# 导出IP核，便于在后续工程中复用
set export_dir [file normalize "$project_root/ipcore_file"]
set export_path [file join $export_dir $top_name]
file mkdir $export_path
puts "Export target: $export_path"
export_design -format ip_catalog -output $export_path

# 将导出的压缩包重命名为与顶层函数一致
set export_zip [file join $export_path export.zip]
if {[file exists $export_zip]} {
    file rename -force $export_zip [file join $export_path "$top_name.zip"]
}

# 解压导出的IP，并删除原压缩包
set final_zip [file join $export_path "$top_name.zip"]
if {[file exists $final_zip]} {
    exec unzip -o $final_zip -d $export_path
    file delete -force $final_zip
}


# 退出Vitis HLS工具
exit


