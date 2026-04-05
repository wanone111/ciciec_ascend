exec sh -c "rm -f flex*.log"
puts "HLS flow start: [clock format [clock seconds]]"

set CUR_DIR [pwd]
set HLS_DIR [file normalize "$CUR_DIR/.."]
set SRC_DIR [file normalize "$HLS_DIR/src"]
set TB_DIR [file normalize "$HLS_DIR/tb"]
set INC_DIR [file normalize "$HLS_DIR/include"]

# workspace 根目录（competition_ws）
set WS_ROOT [file normalize "$HLS_DIR/../../../.."]
set XF_PROJ_ROOT [file normalize "$WS_ROOT/vision"]

if {![file exists "$XF_PROJ_ROOT/L1/include/common/xf_common.hpp"]} {
	puts "ERROR: Vitis Vision headers not found at $XF_PROJ_ROOT/L1/include"
	puts "Please ensure vision repository exists under: $WS_ROOT/vision"
	exit 1
}

open_project -reset vision_pre_hls.prj
set_top vision_preprocess_accel

set CFLAGS "-I$INC_DIR -I$XF_PROJ_ROOT/L1/include -D__SDSVHLS__ -std=c++14"

add_files "$SRC_DIR/top.cpp" -cflags "$CFLAGS" -csimflags "$CFLAGS"
add_files -tb "$TB_DIR/top_tb.cpp" -cflags "$CFLAGS" -csimflags "$CFLAGS"

open_solution -reset solution1
set_part {xc7z020clg400-1}
create_clock -period 10.0

csim_design
csynth_design

# 如需运行 cosim，可将本变量改为 1
set RUN_COSIM 0
if {$RUN_COSIM} {
	cosim_design
}

export_design -flow syn -rtl verilog
puts "HLS flow done: [clock format [clock seconds]]"
exit
# 打开解决方案solution1，如果存在则重置（清空之前的结果）

open_solution -reset solution1

