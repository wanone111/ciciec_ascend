#ifndef PYNQ_VIDEO_PARAMS_H_
#define PYNQ_VIDEO_PARAMS_H_

#include <ap_int.h>

// ============================================================================
// 编译期图像上限，用于约束循环边界和资源估算
// ============================================================================
static const int MAX_WIDTH = 1280;
static const int MAX_HEIGHT = 720;

// ============================================================================
// Gamma 离散模式控制，避免 LUT 额外存储与搬运开销
// ============================================================================
static const ap_uint<8> GAMMA_MODE_BYPASS = 0;
static const ap_uint<8> GAMMA_MODE_BRIGHT = 1;
static const ap_uint<8> GAMMA_MODE_DARK = 2;
static const ap_uint<8> GAMMA_MODE_DARK_STRONG = 3;

// ============================================================================
// Color conversion coefficients (Q8.8 fixed-point)
// BT.601 RGB to YUV conversion coefficients scaled by 256
// ============================================================================
#define CSC_Y_R_COEF  77      // 0.299 * 256
#define CSC_Y_G_COEF  150     // 0.587 * 256
#define CSC_Y_B_COEF  29      // 0.114 * 256
#define CSC_Y_OFFSET  0

#define CSC_U_R_COEF  -43     // -0.169 * 256
#define CSC_U_G_COEF  -85     // -0.331 * 256
#define CSC_U_B_COEF  128     // 0.500 * 256
#define CSC_U_OFFSET  (128 << 8)

#define CSC_V_R_COEF  128     // 0.500 * 256
#define CSC_V_G_COEF  -107    // -0.419 * 256
#define CSC_V_B_COEF  -21     // -0.081 * 256
#define CSC_V_OFFSET  (128 << 8)

// Saturate to [0, 255] after fixed-point arithmetic
static inline ap_uint<8> saturate(int32_t val) {
#pragma HLS INLINE
    if (val < 0) return ap_uint<8>(0);
    if (val > 255) return ap_uint<8>(255);
    return ap_uint<8>(val);
}

// ============================================================================
// ROI Window structure using closed-open interval [x0, x1) x [y0, y1)
// ============================================================================
struct RoiWindow {
    ap_uint<16> x0;
    ap_uint<16> y0;
    ap_uint<16> x1;
    ap_uint<16> y1;
};

// ============================================================================
// Unified Video Control Parameters for HLS IP
// ============================================================================
struct VideoControl {
    // Frame dimensions
    ap_uint<16> rows;
    ap_uint<16> cols;

    // ROI parameters (pixel coordinates)
    ap_uint<16> roi_x;
    ap_uint<16> roi_y;
    ap_uint<16> roi_w;
    ap_uint<16> roi_h;

    // ISP parameters
    ap_uint<16> wb_r_gain;    // Q8 format: 256 = 1.0x
    ap_uint<16> wb_g_gain;
    ap_uint<16> wb_b_gain;
    ap_uint<8> gamma_mode;    // Use GAMMA_MODE_* constants
    ap_uint<8> denoise_strength; // 0-255, higher = more smoothing
    
    // Input format control
    ap_uint<8> input_is_bgr;  // 1 = BGR input, 0 = RGB input
};

#endif  // PYNQ_VIDEO_PARAMS_H_
