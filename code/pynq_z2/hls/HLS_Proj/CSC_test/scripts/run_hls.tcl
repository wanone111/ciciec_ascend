# =============================================================================
# File: run_hls.tcl
# Purpose: Vitis HLS Automation Script - CSC Video Preprocessing
# Target: xc7z020clg400-1, Clock: 100MHz
# Usage: vitis_hls -f run_hls.tcl
# =============================================================================
# Cleanup previous logs
exec sh -c "rm -f flex*.log"
puts "\[INFO\] Start time: [clock format [clock seconds]]"
# ============ Auto-detect project paths (PORTABLE) ============
set script_dir [file dirname [file normalize [info script]]]
set project_root [file normalize "$script_dir/.."]
set src_dir "$project_root/src"
set inc_dir "$project_root/include"
set tb_dir "$project_root/tb"
# ============ Build output isolation ============
set build_root "$project_root/build"
set build_name "hls_video_preprocess"
set build_output "$build_root/$build_name"
file mkdir $build_root
file mkdir $build_output
puts "\[INFO\] Build output: $build_output"
# ============ OpenCV Configuration (Environment-based) ============
# Priority: Environment variable > Auto-detect > Error
if {[info exists ::env(OPENCV_ROOT)]} {
    set opencv_path $::env(OPENCV_ROOT)
    puts "\[INFO\] Using OPENCV_ROOT: $opencv_path"
} else {
    # Try common conda/virtualenv paths
    set possible_paths [list \
        "$::env(CONDA_PREFIX)" \
        "/usr/local" \
        "/usr" \
    ]
    set opencv_path ""
    foreach p $possible_paths {
        if {[file exists "$p/include/opencv4"]} {
            set opencv_path $p
            break
        }
    }
    if {$opencv_path eq ""} {
        puts "ERROR: OpenCV not found. Please set OPENCV_ROOT environment variable."
        puts "Example: export OPENCV_ROOT=/path/to/conda/envs/your_env"
        exit 1
    }
    puts "\[INFO\] Auto-detected OpenCV path: $opencv_path"
}
set opencv_include "$opencv_path/include/opencv4"
set opencv_lib "$opencv_path/lib"
# Validate OpenCV paths
if {![file exists $opencv_include]} {
    puts "ERROR: OpenCV include directory not found: $opencv_include"
    exit 1
}
if {![file exists $opencv_lib]} {
    puts "ERROR: OpenCV lib directory not found: $opencv_lib"
    exit 1
}
set ::env(OPENCV_INCLUDE) "$opencv_include"
set ::env(OPENCV_LIB) "$opencv_lib"
set ::env(LD_LIBRARY_PATH) "$opencv_lib:$::env(LD_LIBRARY_PATH)"
# ============ HLS Project Configuration ============
open_project -reset "$build_output/video_preprocess_prj"
set top_name video_preprocess
set_top $top_name
# Common compiler flags
set common_cflags "-I $inc_dir -I ./ -D__SDSVHLS__ -DXF_USE_AXI4=1 -std=c++17"
add_files "$src_dir/top.cpp" -cflags "$common_cflags"
add_files "$src_dir/rgb2yuv.cpp" -cflags "$common_cflags"
# Testbench with OpenCV
add_files -tb "$tb_dir/tb_top.cpp" -cflags "$common_cflags -I $opencv_include" \
    -csimflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs"
# ============ Solution Setup ============
open_solution -reset solution1
set_part {xc7z020clg400-1}
create_clock -period 10
# ============ Flow Control ============
set run_csim 1
set run_cosim 1
set run_export 0
# C Simulation
if {$run_csim} {
    puts "\[INFO\] Running C Simulation..."
    csim_design -ldflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_video"
}
# C Synthesis
puts "\[INFO\] Running C Synthesis..."
csynth_design
# Co-Simulation
if {$run_cosim} {
    puts "\[INFO\] Running Co-Simulation..."
    cosim_design -ldflags "-L$opencv_lib -Wl,-rpath,$opencv_lib -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_video"
}
# Export RTL (optional)
if {$run_export} {
    puts "\[INFO\] Exporting RTL..."
    export_design -flow syn -rtl verilog
    export_design -flow impl -rtl verilog
}
# ============ IP Export ============
set export_dir [file normalize "$project_root/ipcore_file"]
set export_path [file join $export_dir $top_name]
file mkdir $export_path
puts "\[INFO\] Export target: $export_path"
export_design -format ip_catalog -output $export_path
set export_zip [file join $export_path export.zip]
if {[file exists $export_zip]} {
    file rename -force $export_zip [file join $export_path "$top_name.zip"]
}
set final_zip [file join $export_path "$top_name.zip"]
if {[file exists $final_zip]} {
    exec unzip -o $final_zip -d $export_path
    file delete -force $final_zip
}
puts "\[INFO\] Completed at: [clock format [clock seconds]]"
exit