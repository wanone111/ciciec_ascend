set SynModuleInfo {
  {SRCNAME vision_preprocess_accel_Pipeline_VITIS_LOOP_50_1_VITIS_LOOP_52_2 MODELNAME vision_preprocess_accel_Pipeline_VITIS_LOOP_50_1_VITIS_LOOP_52_2 RTLNAME vision_preprocess_accel_vision_preprocess_accel_Pipeline_VITIS_LOOP_50_1_VITIS_LOOP_52_2
    SUBMODULES {
      {MODELNAME vision_preprocess_accel_mul_32ns_32s_62_2_1 RTLNAME vision_preprocess_accel_mul_32ns_32s_62_2_1 BINDTYPE op TYPE mul IMPL auto LATENCY 1 ALLOW_PRAGMA 1}
      {MODELNAME vision_preprocess_accel_mul_8ns_8ns_15_1_1 RTLNAME vision_preprocess_accel_mul_8ns_8ns_15_1_1 BINDTYPE op TYPE mul IMPL auto LATENCY 0 ALLOW_PRAGMA 1}
      {MODELNAME vision_preprocess_accel_mac_muladd_8ns_5ns_15s_15_4_1 RTLNAME vision_preprocess_accel_mac_muladd_8ns_5ns_15s_15_4_1 BINDTYPE op TYPE all IMPL dsp_slice LATENCY 3}
      {MODELNAME vision_preprocess_accel_mac_muladd_8ns_8ns_15ns_16_4_1 RTLNAME vision_preprocess_accel_mac_muladd_8ns_8ns_15ns_16_4_1 BINDTYPE op TYPE all IMPL dsp_slice LATENCY 3}
      {MODELNAME vision_preprocess_accel_flow_control_loop_pipe_sequential_init RTLNAME vision_preprocess_accel_flow_control_loop_pipe_sequential_init BINDTYPE interface TYPE internal_upc_flow_control INSTNAME vision_preprocess_accel_flow_control_loop_pipe_sequential_init_U}
    }
  }
  {SRCNAME vision_preprocess_accel MODELNAME vision_preprocess_accel RTLNAME vision_preprocess_accel IS_TOP 1
    SUBMODULES {
      {MODELNAME vision_preprocess_accel_mul_31ns_31ns_62_2_1 RTLNAME vision_preprocess_accel_mul_31ns_31ns_62_2_1 BINDTYPE op TYPE mul IMPL auto LATENCY 1 ALLOW_PRAGMA 1}
      {MODELNAME vision_preprocess_accel_gmem0_m_axi RTLNAME vision_preprocess_accel_gmem0_m_axi BINDTYPE interface TYPE adapter IMPL m_axi}
      {MODELNAME vision_preprocess_accel_gmem1_m_axi RTLNAME vision_preprocess_accel_gmem1_m_axi BINDTYPE interface TYPE adapter IMPL m_axi}
      {MODELNAME vision_preprocess_accel_control_s_axi RTLNAME vision_preprocess_accel_control_s_axi BINDTYPE interface TYPE interface_s_axilite}
    }
  }
}
