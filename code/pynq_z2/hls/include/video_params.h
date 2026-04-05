#ifndef VIDEO_PARAMS_H
#define VIDEO_PARAMS_H

#include <ap_int.h>

// PYNQ-Z2 输入分辨率上限（可按赛题调整）
#define MAX_HEIGHT 720
#define MAX_WIDTH 1280

#define INPUT_PTR_WIDTH 32
#define OUTPUT_PTR_WIDTH 8

#define NPPCX XF_NPPC1
#define IN_TYPE XF_8UC3
#define OUT_TYPE XF_8UC1

#define XF_CV_DEPTH_IN 2
#define XF_CV_DEPTH_GRAY 2
#define XF_CV_DEPTH_OUT 2

// 默认阈值用于 testbench 快速验证
static const ap_uint<8> kDefaultThreshold = 128;

#endif
