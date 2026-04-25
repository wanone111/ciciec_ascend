// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2.1 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2026 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
// control
// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read/COR)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read/COR)
//        bit 7  - auto_restart (Read/Write)
//        bit 9  - interrupt (Read)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0 - enable ap_done interrupt (Read/Write)
//        bit 1 - enable ap_ready interrupt (Read/Write)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0 - ap_done (Read/TOW)
//        bit 1 - ap_ready (Read/TOW)
//        others - reserved
// 0x10 : Data signal of rows
//        bit 15~0 - rows[15:0] (Read/Write)
//        others   - reserved
// 0x14 : reserved
// 0x18 : Data signal of cols
//        bit 15~0 - cols[15:0] (Read/Write)
//        others   - reserved
// 0x1c : reserved
// 0x20 : Data signal of roi_x
//        bit 15~0 - roi_x[15:0] (Read/Write)
//        others   - reserved
// 0x24 : reserved
// 0x28 : Data signal of roi_y
//        bit 15~0 - roi_y[15:0] (Read/Write)
//        others   - reserved
// 0x2c : reserved
// 0x30 : Data signal of roi_w
//        bit 15~0 - roi_w[15:0] (Read/Write)
//        others   - reserved
// 0x34 : reserved
// 0x38 : Data signal of roi_h
//        bit 15~0 - roi_h[15:0] (Read/Write)
//        others   - reserved
// 0x3c : reserved
// 0x40 : Data signal of wb_r_gain
//        bit 15~0 - wb_r_gain[15:0] (Read/Write)
//        others   - reserved
// 0x44 : reserved
// 0x48 : Data signal of wb_g_gain
//        bit 15~0 - wb_g_gain[15:0] (Read/Write)
//        others   - reserved
// 0x4c : reserved
// 0x50 : Data signal of wb_b_gain
//        bit 15~0 - wb_b_gain[15:0] (Read/Write)
//        others   - reserved
// 0x54 : reserved
// 0x58 : Data signal of gamma_mode
//        bit 7~0 - gamma_mode[7:0] (Read/Write)
//        others  - reserved
// 0x5c : reserved
// 0x60 : Data signal of denoise_strength
//        bit 7~0 - denoise_strength[7:0] (Read/Write)
//        others  - reserved
// 0x64 : reserved
// 0x68 : Data signal of input_is_bgr
//        bit 7~0 - input_is_bgr[7:0] (Read/Write)
//        others  - reserved
// 0x6c : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define XVIDEO_PREPROCESS_CONTROL_ADDR_AP_CTRL               0x00
#define XVIDEO_PREPROCESS_CONTROL_ADDR_GIE                   0x04
#define XVIDEO_PREPROCESS_CONTROL_ADDR_IER                   0x08
#define XVIDEO_PREPROCESS_CONTROL_ADDR_ISR                   0x0c
#define XVIDEO_PREPROCESS_CONTROL_ADDR_ROWS_DATA             0x10
#define XVIDEO_PREPROCESS_CONTROL_BITS_ROWS_DATA             16
#define XVIDEO_PREPROCESS_CONTROL_ADDR_COLS_DATA             0x18
#define XVIDEO_PREPROCESS_CONTROL_BITS_COLS_DATA             16
#define XVIDEO_PREPROCESS_CONTROL_ADDR_ROI_X_DATA            0x20
#define XVIDEO_PREPROCESS_CONTROL_BITS_ROI_X_DATA            16
#define XVIDEO_PREPROCESS_CONTROL_ADDR_ROI_Y_DATA            0x28
#define XVIDEO_PREPROCESS_CONTROL_BITS_ROI_Y_DATA            16
#define XVIDEO_PREPROCESS_CONTROL_ADDR_ROI_W_DATA            0x30
#define XVIDEO_PREPROCESS_CONTROL_BITS_ROI_W_DATA            16
#define XVIDEO_PREPROCESS_CONTROL_ADDR_ROI_H_DATA            0x38
#define XVIDEO_PREPROCESS_CONTROL_BITS_ROI_H_DATA            16
#define XVIDEO_PREPROCESS_CONTROL_ADDR_WB_R_GAIN_DATA        0x40
#define XVIDEO_PREPROCESS_CONTROL_BITS_WB_R_GAIN_DATA        16
#define XVIDEO_PREPROCESS_CONTROL_ADDR_WB_G_GAIN_DATA        0x48
#define XVIDEO_PREPROCESS_CONTROL_BITS_WB_G_GAIN_DATA        16
#define XVIDEO_PREPROCESS_CONTROL_ADDR_WB_B_GAIN_DATA        0x50
#define XVIDEO_PREPROCESS_CONTROL_BITS_WB_B_GAIN_DATA        16
#define XVIDEO_PREPROCESS_CONTROL_ADDR_GAMMA_MODE_DATA       0x58
#define XVIDEO_PREPROCESS_CONTROL_BITS_GAMMA_MODE_DATA       8
#define XVIDEO_PREPROCESS_CONTROL_ADDR_DENOISE_STRENGTH_DATA 0x60
#define XVIDEO_PREPROCESS_CONTROL_BITS_DENOISE_STRENGTH_DATA 8
#define XVIDEO_PREPROCESS_CONTROL_ADDR_INPUT_IS_BGR_DATA     0x68
#define XVIDEO_PREPROCESS_CONTROL_BITS_INPUT_IS_BGR_DATA     8

