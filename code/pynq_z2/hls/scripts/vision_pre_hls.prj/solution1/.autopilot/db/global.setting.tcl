
set TopModule "vision_preprocess_accel"
set ClockPeriod 10
set ClockList ap_clk
set AxiliteClockList {}
set HasVivadoClockPeriod 0
set CombLogicFlag 0
set PipelineFlag 0
set DataflowTaskPipelineFlag 1
set TrivialPipelineFlag 0
set noPortSwitchingFlag 0
set FloatingPointFlag 0
set FftOrFirFlag 0
set NbRWValue 0
set intNbAccess 0
set NewDSPMapping 1
set HasDSPModule 1
set ResetLevelFlag 0
set ResetStyle control
set ResetSyncFlag 1
set ResetRegisterFlag 0
set ResetVariableFlag 0
set ResetRegisterNum 0
set FsmEncStyle onehot
set MaxFanout 0
set RtlPrefix {}
set RtlSubPrefix vision_preprocess_accel_
set ExtraCCFlags {}
set ExtraCLdFlags {}
set SynCheckOptions {}
set PresynOptions {}
set PreprocOptions {}
set RtlWriterOptions {}
set CbcGenFlag 0
set CasGenFlag 0
set CasMonitorFlag 0
set AutoSimOptions {}
set ExportMCPathFlag 0
set SCTraceFileName mytrace
set SCTraceFileFormat vcd
set SCTraceOption all
set TargetInfo xc7z020:-clg400:-1
set SourceFiles {sc {} c {../.././../src/top.cpp ../../../src/top.cpp}}
set SourceFlags {sc {} c {{ -I/home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/include  -I/home/bb/competition_ws/vision/L1/include  -D__SDSVHLS__ -std=c++14} {-I/home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/include -I/home/bb/competition_ws/vision/L1/include -D__SDSVHLS__ -std=c++14} {-I/home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/include -I/home/bb/competition_ws/vision/L1/include -D__SDSVHLS__ -std=c++14}}}
set DirectiveFile {}
set TBFiles {verilog /home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/tb/top_tb.cpp bc /home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/tb/top_tb.cpp sc /home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/tb/top_tb.cpp vhdl /home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/tb/top_tb.cpp c {} cas /home/bb/competition_ws/ciciec_ascend/code/pynq_z2/hls/tb/top_tb.cpp}
set SpecLanguage C
set TVInFiles {bc {} c {} sc {} cas {} vhdl {} verilog {}}
set TVOutFiles {bc {} c {} sc {} cas {} vhdl {} verilog {}}
set TBTops {verilog {} bc {} sc {} vhdl {} c {} cas {}}
set TBInstNames {verilog {} bc {} sc {} vhdl {} c {} cas {}}
set XDCFiles {}
set ExtraGlobalOptions {"area_timing" 1 "clock_gate" 1 "impl_flow" map "power_gate" 0}
set TBTVFileNotFound {}
set AppFile ../hls.app
set ApsFile solution1.aps
set AvePath ../../.
set DefaultPlatform DefaultPlatform
set multiClockList {}
set SCPortClockMap {}
set intNbAccess 0
set PlatformFiles {{DefaultPlatform {xilinx/zynq/zynq}}}
set HPFPO 0
