// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2.1 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2026 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef XVIDEO_PREPROCESS_H
#define XVIDEO_PREPROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#ifndef __linux__
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xil_io.h"
#else
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stddef.h>
#endif
#include "xvideo_preprocess_hw.h"

/**************************** Type Definitions ******************************/
#ifdef __linux__
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
typedef struct {
#ifdef SDT
    char *Name;
#else
    u16 DeviceId;
#endif
    u64 Control_BaseAddress;
} XVideo_preprocess_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XVideo_preprocess;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XVideo_preprocess_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XVideo_preprocess_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XVideo_preprocess_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XVideo_preprocess_ReadReg(BaseAddress, RegOffset) \
    *(volatile u32*)((BaseAddress) + (RegOffset))

#define Xil_AssertVoid(expr)    assert(expr)
#define Xil_AssertNonvoid(expr) assert(expr)

#define XST_SUCCESS             0
#define XST_DEVICE_NOT_FOUND    2
#define XST_OPEN_DEVICE_FAILED  3
#define XIL_COMPONENT_IS_READY  1
#endif

/************************** Function Prototypes *****************************/
#ifndef __linux__
#ifdef SDT
int XVideo_preprocess_Initialize(XVideo_preprocess *InstancePtr, UINTPTR BaseAddress);
XVideo_preprocess_Config* XVideo_preprocess_LookupConfig(UINTPTR BaseAddress);
#else
int XVideo_preprocess_Initialize(XVideo_preprocess *InstancePtr, u16 DeviceId);
XVideo_preprocess_Config* XVideo_preprocess_LookupConfig(u16 DeviceId);
#endif
int XVideo_preprocess_CfgInitialize(XVideo_preprocess *InstancePtr, XVideo_preprocess_Config *ConfigPtr);
#else
int XVideo_preprocess_Initialize(XVideo_preprocess *InstancePtr, const char* InstanceName);
int XVideo_preprocess_Release(XVideo_preprocess *InstancePtr);
#endif

void XVideo_preprocess_Start(XVideo_preprocess *InstancePtr);
u32 XVideo_preprocess_IsDone(XVideo_preprocess *InstancePtr);
u32 XVideo_preprocess_IsIdle(XVideo_preprocess *InstancePtr);
u32 XVideo_preprocess_IsReady(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_EnableAutoRestart(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_DisableAutoRestart(XVideo_preprocess *InstancePtr);

void XVideo_preprocess_Set_rows(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_rows(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_cols(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_cols(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_roi_x(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_roi_x(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_roi_y(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_roi_y(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_roi_w(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_roi_w(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_roi_h(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_roi_h(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_wb_r_gain(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_wb_r_gain(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_wb_g_gain(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_wb_g_gain(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_wb_b_gain(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_wb_b_gain(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_gamma_mode(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_gamma_mode(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_denoise_strength(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_denoise_strength(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_Set_input_is_bgr(XVideo_preprocess *InstancePtr, u32 Data);
u32 XVideo_preprocess_Get_input_is_bgr(XVideo_preprocess *InstancePtr);

void XVideo_preprocess_InterruptGlobalEnable(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_InterruptGlobalDisable(XVideo_preprocess *InstancePtr);
void XVideo_preprocess_InterruptEnable(XVideo_preprocess *InstancePtr, u32 Mask);
void XVideo_preprocess_InterruptDisable(XVideo_preprocess *InstancePtr, u32 Mask);
void XVideo_preprocess_InterruptClear(XVideo_preprocess *InstancePtr, u32 Mask);
u32 XVideo_preprocess_InterruptGetEnabled(XVideo_preprocess *InstancePtr);
u32 XVideo_preprocess_InterruptGetStatus(XVideo_preprocess *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
