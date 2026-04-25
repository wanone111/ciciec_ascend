// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2.1 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2026 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#ifdef SDT
#include "xparameters.h"
#endif
#include "xvideo_preprocess.h"

extern XVideo_preprocess_Config XVideo_preprocess_ConfigTable[];

#ifdef SDT
XVideo_preprocess_Config *XVideo_preprocess_LookupConfig(UINTPTR BaseAddress) {
	XVideo_preprocess_Config *ConfigPtr = NULL;

	int Index;

	for (Index = (u32)0x0; XVideo_preprocess_ConfigTable[Index].Name != NULL; Index++) {
		if (!BaseAddress || XVideo_preprocess_ConfigTable[Index].Control_BaseAddress == BaseAddress) {
			ConfigPtr = &XVideo_preprocess_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XVideo_preprocess_Initialize(XVideo_preprocess *InstancePtr, UINTPTR BaseAddress) {
	XVideo_preprocess_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XVideo_preprocess_LookupConfig(BaseAddress);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XVideo_preprocess_CfgInitialize(InstancePtr, ConfigPtr);
}
#else
XVideo_preprocess_Config *XVideo_preprocess_LookupConfig(u16 DeviceId) {
	XVideo_preprocess_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XVIDEO_PREPROCESS_NUM_INSTANCES; Index++) {
		if (XVideo_preprocess_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XVideo_preprocess_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XVideo_preprocess_Initialize(XVideo_preprocess *InstancePtr, u16 DeviceId) {
	XVideo_preprocess_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XVideo_preprocess_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XVideo_preprocess_CfgInitialize(InstancePtr, ConfigPtr);
}
#endif

#endif

