# =============================================================================
# File: /home/bb/competition_ws/pynq_z2/hls/video_preprocess/scripts/run_hls.tcl
# Purpose: Vitis HLS 一键流水线脚本 - 视频预处理 (PYNQ-Z2)
# Target: xc7z020clg400-1, Clock: 100MHz, Flow: CSIM→CSYNTH→COSIM→IP Export
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

# ============ 构建输出路径隔离 ============
set build_root "$project_root/build"
set build_name "hls_video_preprocess"
set build_output "$build_root/$build_name"
file mkdir $build_root
file mkdir $build_output
puts "Build output redirected to: $build_output"

# ============ 可选 OpenCV 环境配置 ============
set opencv_available 0
set opencv_path ""
set opencv_include ""
set opencv_lib ""

if {[info exists ::env(OPENCV_ROOT)] && [file exists "$::env(OPENCV_ROOT)/include/opencv4"]} {
    set opencv_path $::env(OPENCV_ROOT)
    set opencv_include "$opencv_path/include/opencv4"
    set opencv_lib "$opencv_path/lib"
    set opencv_available 1
} elseif {[info exists ::env(CONDA_PREFIX)] && [file exists "$::env(CONDA_PREFIX)/include/opencv4"]} {
    set opencv_path $::env(CONDA_PREFIX)
    set opencv_include "$opencv_path/include/opencv4"
    set opencv_lib "$opencv_path/lib"
    set opencv_available 1
} elseif {[file exists "/home/bb/miniconda3/envs/310B_env/include/opencv4"]} {
    set opencv_path "/home/bb/miniconda3/envs/310B_env"
    set opencv_include "$opencv_path/include/opencv4"
    set opencv_lib "$opencv_path/lib"
    set opencv_available 1
}

if {$opencv_available} {
    set ::env(OPENCV_INCLUDE) "$opencv_include"
    set ::env(OPENCV_LIB) "$opencv_lib"
    if {[info exists ::env(LD_LIBRARY_PATH)]} {
        set ::env(LD_LIBRARY_PATH) "$opencv_lib:$::env(LD_LIBRARY_PATH)"
    } else {
        set ::env(LD_LIBRARY_PATH) "$opencv_lib"
    }
    puts "OpenCV found: $opencv_path"
} else {
    puts "OpenCV not found, testbench will use pure C++ mode"
}

# ============ HLS 项目配置部分 ============
open_project -reset "$build_output/video_preprocess_prj"

# 设置顶层函数名为（即要综合的主函数）
set top_name video_preprocess
set_top $top_name

# 添加设计源文件
set common_cflags "-I $inc_dir -I ./ -D__SDSVHLS__ -DXF_USE_AXI4=1 -std=c++17"
add_files "$src_dir/top.cpp" -cflags "$common_cflags"

# 添加测试文件，并在可用时附加 OpenCV 编译/链接参数
set tb_cflags "$common_cflags"
if {$opencv_available} {
    append tb_cflags " -I $opencv_include"
    set tb_csimflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs"
    add_files -tb "$tb_dir/tb_top.cpp" -cflags "$tb_cflags" -csimflags "$tb_csimflags"
} else {
    add_files -tb "$tb_dir/tb_top.cpp" -cflags "$tb_cflags"
}

# 打开解决方案solution1，如果存在则重置（清空之前的结果）
open_solution -reset solution1

# 设置目标FPGA器件
set_part {xc7z020clg400-1}

# 创建时钟约束，设置时钟周期为10ns（100MHz）
create_clock -period 10

# ============ HLS流程执行部分 ============
set run_csim 1
set run_csynth 1
set run_cosim 1
set run_export 0

if {$run_csim} {
    puts "\n========== Running C Simulation (CSIM) =========="
    if {$opencv_available} {
        csim_design -ldflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs"
    } else {
        csim_design
    }
}

if {$run_csynth} {
    puts "\n========== Running C Synthesis (CSYNTH) =========="
    csynth_design
}

if {$run_cosim} {
    puts "\n========== Running C/RTL Co-Simulation (COSIM) =========="
    if {$opencv_available} {
        cosim_design -ldflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs"
    } else {
        cosim_design
    }
}

# 导出综合级RTL（仅逻辑综合，用于快速Vivado IP集成）
if {$run_export} {
    puts "\n========== Exporting Synthesis-level RTL =========="
    export_design -flow syn -rtl verilog
}

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

exit