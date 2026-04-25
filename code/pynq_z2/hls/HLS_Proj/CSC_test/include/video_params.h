#ifndef PYNQ_VIDEO_PARAMS_H_
#define PYNQ_VIDEO_PARAMS_H_

#include <ap_int.h>
#include <cstdint>

// Compile-time bounds for HLS buffer sizing.
static const int MAX_WIDTH = 1280;
static const int MAX_HEIGHT = 720;

// ================= Color conversion coefficients (Q8.8) =================
#define CSC_Y_R_COEF  77
#define CSC_Y_G_COEF  150
#define CSC_Y_B_COEF  29
#define CSC_Y_OFFSET  0

#define CSC_U_R_COEF  -43
#define CSC_U_G_COEF  -85
#define CSC_U_B_COEF  128
#define CSC_U_OFFSET  (128 << 8)

#define CSC_V_R_COEF  128
#define CSC_V_G_COEF  -107
#define CSC_V_B_COEF  -21
#define CSC_V_OFFSET  (128 << 8)

// Saturate to [0, 255] after fixed-point arithmetic.
static inline ap_uint<8> saturate(int32_t val) {
    #pragma HLS INLINE  // ✅ 移入函数体第一行
    if (val < 0) {
        return ap_uint<8>(0);
    }
    if (val > 255) {
        return ap_uint<8>(255);
    }
    return ap_uint<8>(val);
}

struct VideoControl {
	ap_uint<16> rows;
	ap_uint<16> cols;

	ap_uint<16> roi_x;
	ap_uint<16> roi_y;
	ap_uint<16> roi_w;
	ap_uint<16> roi_h;

	ap_uint<8> gray_en;
	ap_uint<8> threshold_en;
	ap_uint<8> threshold_val;
};

#endif  // PYNQ_VIDEO_PARAMS_H_
