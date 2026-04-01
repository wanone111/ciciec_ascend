
# 删除之前运行产生的日志文件，清理工作环境
exec sh -c "rm -f flex*.log"

puts "vision_pre HLS flow start time: [clock format [clock seconds]]"

# ============ OpenCV环境配置部分 ============
set opencv_include "/usr/include/opencv4"
set opencv_lib_flags "-L/usr/lib/x86_64-linux-gnu -Wl,-rpath,/usr/lib/x86_64-linux-gnu -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lstdc++"
set xf_vision_include "/home/bb/competition_ws/vision/L1/include"

# ============ HLS项目配置部分 ============
open_project vision_pre.prj
set_top vision_pre

add_files ../src/vision_pre.cpp -cflags "-I../src/include -I$xf_vision_include -std=c++14"
add_files -tb ../src/main.cpp -cflags "-I../src/include -I$xf_vision_include -I$opencv_include -std=c++14" -csimflags "$opencv_lib_flags"

open_solution -reset solution1
set_part {xc7z020clg400-1}
create_clock -period 10 -name default

# ============ HLS流程执行部分 ============
csim_design -ldflags "$opencv_lib_flags"
csynth_design
cosim_design -ldflags "$opencv_lib_flags"

exit


