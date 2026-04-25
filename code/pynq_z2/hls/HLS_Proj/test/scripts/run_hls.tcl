# =============================================================================
# ROI_Test clean baseline
# Run command:
#   vitis-run --mode hls --tcl run_hls.tcl
# Flow:
#   CSIM + CSYNTH only (NO IP export)
# =============================================================================

catch {exec sh -c "rm -f flex*.log *.jou *.log"}
puts "Start time: [clock format [clock seconds]]"

set script_dir   [file dirname [file normalize [info script]]]
set project_root [file normalize "$script_dir/.."]
set src_dir      "$project_root/src"
set inc_dir      "$project_root/include"
set tb_dir       "$project_root/tb"
set build_root   "$project_root/build"
set build_name   "hls_video_preprocess_clean"
set build_output "$build_root/$build_name"

file mkdir $build_root
file mkdir $build_output
puts "Project root : $project_root"
puts "Build output : $build_output"

open_project -reset "$build_output/video_preprocess_prj"
set_top video_preprocess

set common_cflags "-I $inc_dir -I $src_dir -D__SDSVHLS__ -std=c++17"
add_files "$src_dir/top.cpp" -cflags "$common_cflags"
add_files "$src_dir/roi.cpp" -cflags "$common_cflags"
add_files "$src_dir/isp.cpp" -cflags "$common_cflags"
add_files -tb "$tb_dir/tb_top.cpp" -cflags "$common_cflags"

open_solution -reset solution1
set_part {xc7z020clg400-1}
create_clock -period 10.0

set RUN_CSIM   1
set RUN_CSYNTH 1
set RUN_COSIM  0
set RUN_EXPORT 0

if {$RUN_CSIM} {
    puts "========== CSIM =========="
    csim_design -clean
}

if {$RUN_CSYNTH} {
    puts "========== CSYNTH =========="
    csynth_design
}

if {$RUN_COSIM} {
    puts "========== COSIM =========="
    cosim_design
}

if {$RUN_EXPORT} {
    puts "========== EXPORT =========="
    export_design -format ip_catalog
}

puts "Done at: [clock format [clock seconds]]"
exit
