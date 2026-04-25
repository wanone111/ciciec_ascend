// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2.1 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2026 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xvideo_preprocess.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XVideo_preprocess_CfgInitialize(XVideo_preprocess *InstancePtr, XVideo_preprocess_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XVideo_preprocess_Start(XVideo_preprocess *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_AP_CTRL) & 0x80;
    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XVideo_preprocess_IsDone(XVideo_preprocess *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XVideo_preprocess_IsIdle(XVideo_preprocess *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XVideo_preprocess_IsReady(XVideo_preprocess *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XVideo_preprocess_EnableAutoRestart(XVideo_preprocess *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XVideo_preprocess_DisableAutoRestart(XVideo_preprocess *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_AP_CTRL, 0);
}

void XVideo_preprocess_Set_rows(XVideo_preprocess *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_ROWS_DATA, Data);
}

u32 XVideo_preprocess_Get_rows(XVideo_preprocess *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_ROWS_DATA);
    return Data;
}

void XVideo_preprocess_Set_cols(XVideo_preprocess *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_COLS_DATA, Data);
}

u32 XVideo_preprocess_Get_cols(XVideo_preprocess *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_COLS_DATA);
    return Data;
}

void XVideo_preprocess_Set_input_is_bgr(XVideo_preprocess *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_INPUT_IS_BGR_DATA, Data);
}

u32 XVideo_preprocess_Get_input_is_bgr(XVideo_preprocess *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_INPUT_IS_BGR_DATA);
    return Data;
}

void XVideo_preprocess_InterruptGlobalEnable(XVideo_preprocess *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_GIE, 1);
}

void XVideo_preprocess_InterruptGlobalDisable(XVideo_preprocess *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_GIE, 0);
}

void XVideo_preprocess_InterruptEnable(XVideo_preprocess *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_IER);
    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_IER, Register | Mask);
}

void XVideo_preprocess_InterruptDisable(XVideo_preprocess *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_IER);
    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_IER, Register & (~Mask));
}

void XVideo_preprocess_InterruptClear(XVideo_preprocess *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XVideo_preprocess_WriteReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_ISR, Mask);
}

u32 XVideo_preprocess_InterruptGetEnabled(XVideo_preprocess *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_IER);
}

u32 XVideo_preprocess_InterruptGetStatus(XVideo_preprocess *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XVideo_preprocess_ReadReg(InstancePtr->Control_BaseAddress, XVIDEO_PREPROCESS_CONTROL_ADDR_ISR);
}

