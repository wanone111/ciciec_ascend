<AutoPilot:project xmlns:AutoPilot="com.autoesl.autopilot.project" top="vision_preprocess_accel" name="vision_pre_hls.prj" ideType="classic">
    <files>
        <file name="../src/top.cpp" sc="0" tb="false" cflags="-I/home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/include -I/home/bb/competition_ws/vision/L1/include -D__SDSVHLS__ -std=c++14" csimflags="-I/home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/include -I/home/bb/competition_ws/vision/L1/include -D__SDSVHLS__ -std=c++14" blackbox="false"/>
        <file name="/home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/tb/top_tb.cpp" sc="0" tb="1" cflags="-I/home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/include -I/home/bb/competition_ws/vision/L1/include -D__SDSVHLS__ -std=c++14 -I/home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/include -I/home/bb/competition_ws/vision/L1/include -D__SDSVHLS__ -std=c++14 -Wno-unknown-pragmas" csimflags="" blackbox="false"/>
    </files>
    <solutions>
        <solution name="solution1" status=""/>
    </solutions>
    <Simulation argv="">
        <SimFlow name="csim" setup="false" optimizeCompile="false" clean="false" ldflags="" mflags=""/>
    </Simulation>
</AutoPilot:project>
