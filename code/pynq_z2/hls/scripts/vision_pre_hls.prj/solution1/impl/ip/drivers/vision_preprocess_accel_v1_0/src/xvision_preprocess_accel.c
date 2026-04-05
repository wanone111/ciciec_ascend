// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2.1 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2026 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xvision_preprocess_accel.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XVision_preprocess_accel_CfgInitialize(XVision_preprocess_accel *InstancePtr, XVision_preprocess_accel_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XVision_preprocess_accel_Start(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_AP_CTRL) & 0x80;
    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XVision_preprocess_accel_IsDone(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XVision_preprocess_accel_IsIdle(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XVision_preprocess_accel_IsReady(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XVision_preprocess_accel_EnableAutoRestart(XVision_preprocess_accel *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XVision_preprocess_accel_DisableAutoRestart(XVision_preprocess_accel *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_AP_CTRL, 0);
}

void XVision_preprocess_accel_Set_img_in(XVision_preprocess_accel *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IMG_IN_DATA, (u32)(Data));
    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IMG_IN_DATA + 4, (u32)(Data >> 32));
}

u64 XVision_preprocess_accel_Get_img_in(XVision_preprocess_accel *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IMG_IN_DATA);
    Data += (u64)XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IMG_IN_DATA + 4) << 32;
    return Data;
}

void XVision_preprocess_accel_Set_img_out(XVision_preprocess_accel *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IMG_OUT_DATA, (u32)(Data));
    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IMG_OUT_DATA + 4, (u32)(Data >> 32));
}

u64 XVision_preprocess_accel_Get_img_out(XVision_preprocess_accel *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IMG_OUT_DATA);
    Data += (u64)XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IMG_OUT_DATA + 4) << 32;
    return Data;
}

void XVision_preprocess_accel_Set_rows(XVision_preprocess_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROWS_DATA, Data);
}

u32 XVision_preprocess_accel_Get_rows(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROWS_DATA);
    return Data;
}

void XVision_preprocess_accel_Set_cols(XVision_preprocess_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_COLS_DATA, Data);
}

u32 XVision_preprocess_accel_Get_cols(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_COLS_DATA);
    return Data;
}

void XVision_preprocess_accel_Set_roi_x(XVision_preprocess_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROI_X_DATA, Data);
}

u32 XVision_preprocess_accel_Get_roi_x(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROI_X_DATA);
    return Data;
}

void XVision_preprocess_accel_Set_roi_y(XVision_preprocess_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROI_Y_DATA, Data);
}

u32 XVision_preprocess_accel_Get_roi_y(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROI_Y_DATA);
    return Data;
}

void XVision_preprocess_accel_Set_roi_w(XVision_preprocess_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROI_W_DATA, Data);
}

u32 XVision_preprocess_accel_Get_roi_w(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROI_W_DATA);
    return Data;
}

void XVision_preprocess_accel_Set_roi_h(XVision_preprocess_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROI_H_DATA, Data);
}

u32 XVision_preprocess_accel_Get_roi_h(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ROI_H_DATA);
    return Data;
}

void XVision_preprocess_accel_Set_thresh_val(XVision_preprocess_accel *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_THRESH_VAL_DATA, Data);
}

u32 XVision_preprocess_accel_Get_thresh_val(XVision_preprocess_accel *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_THRESH_VAL_DATA);
    return Data;
}

void XVision_preprocess_accel_InterruptGlobalEnable(XVision_preprocess_accel *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_GIE, 1);
}

void XVision_preprocess_accel_InterruptGlobalDisable(XVision_preprocess_accel *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_GIE, 0);
}

void XVision_preprocess_accel_InterruptEnable(XVision_preprocess_accel *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IER);
    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IER, Register | Mask);
}

void XVision_preprocess_accel_InterruptDisable(XVision_preprocess_accel *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IER);
    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IER, Register & (~Mask));
}

void XVision_preprocess_accel_InterruptClear(XVision_preprocess_accel *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVision_preprocess_accel_WriteReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ISR, Mask);
}

u32 XVision_preprocess_accel_InterruptGetEnabled(XVision_preprocess_accel *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_IER);
}

u32 XVision_preprocess_accel_InterruptGetStatus(XVision_preprocess_accel *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XVision_preprocess_accel_ReadReg(InstancePtr->Control_BaseAddress, XVISION_PREPROCESS_ACCEL_CONTROL_ADDR_ISR);
}

