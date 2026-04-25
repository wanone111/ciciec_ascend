#ifndef PYNQ_VIDEO_PARAMS_H_
#define PYNQ_VIDEO_PARAMS_H_

#include <ap_int.h>

// HLS 编译期图像上限，用于约束循环边界和资源估算。
static const int MAX_WIDTH = 1280;
static const int MAX_HEIGHT = 720;

// Gamma 采用离散模式控制，避免 LUT 额外存储与搬运开销。
static const ap_uint<8> GAMMA_MODE_BYPASS = 0;
static const ap_uint<8> GAMMA_MODE_BRIGHT = 1;
static const ap_uint<8> GAMMA_MODE_DARK = 2;
static const ap_uint<8> GAMMA_MODE_DARK_STRONG = 3;

struct VideoControl {
	ap_uint<16> rows;
	ap_uint<16> cols;

	ap_uint<16> roi_x;
	ap_uint<16> roi_y;
	ap_uint<16> roi_w;
	ap_uint<16> roi_h;

	// 白平衡增益，Q8 定点格式（256 表示 1.0x）。
	ap_uint<16> wb_r_gain;
	ap_uint<16> wb_g_gain;
	ap_uint<16> wb_b_gain;
	// gamma_mode: 使用上面的离散模式常量。
	ap_uint<8> gamma_mode;
	// 简易降噪强度，0~255，值越大历史像素权重越高。
	ap_uint<8> denoise_strength;

	ap_uint<8> gray_en;
	ap_uint<8> threshold_en;
	ap_uint<8> threshold_val;
};

#endif  // PYNQ_VIDEO_PARAMS_H_
