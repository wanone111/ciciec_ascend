#ifndef PYNQ_RGB2YUV_H_
#define PYNQ_RGB2YUV_H_

#include <ap_int.h>

#include "video_params.h"

// RGB/BGR unpack helper.
// NOTE: This version intentionally keeps the original channel mapping unchanged.
static inline void unpack_rgb(ap_uint<24> pix, ap_uint<1> input_is_bgr,
                              ap_uint<8>& r, ap_uint<8>& g, ap_uint<8>& b) {
#pragma HLS INLINE
    ap_uint<8> ch0 = pix.range(7, 0);
    ap_uint<8> ch1 = pix.range(15, 8);
    ap_uint<8> ch2 = pix.range(23, 16);
    r = input_is_bgr ? ch2 : ch0;
    g = ch1;
    b = input_is_bgr ? ch0 : ch2;
}

// BT.601 Q8.8 RGB->YUV conversion helper.
static inline void calc_yuv_parallel(ap_uint<8> r, ap_uint<8> g, ap_uint<8> b,
                                     ap_uint<8>& y_out, ap_uint<8>& u_out, ap_uint<8>& v_out) {
#pragma HLS INLINE

    int32_t y_acc = CSC_Y_R_COEF * (int32_t)r + CSC_Y_G_COEF * (int32_t)g + CSC_Y_B_COEF * (int32_t)b + CSC_Y_OFFSET;
    y_out = saturate((y_acc + 128) >> 8);

    int32_t u_acc = CSC_U_R_COEF * (int32_t)r + CSC_U_G_COEF * (int32_t)g + CSC_U_B_COEF * (int32_t)b + CSC_U_OFFSET;
    u_out = saturate((u_acc + 128) >> 8);

    int32_t v_acc = CSC_V_R_COEF * (int32_t)r + CSC_V_G_COEF * (int32_t)g + CSC_V_B_COEF * (int32_t)b + CSC_V_OFFSET;
    v_out = saturate((v_acc + 128) >> 8);
}

#endif  // PYNQ_RGB2YUV_H_
