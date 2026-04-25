#include "isp.h"

ap_uint<8> clamp_u8(int v) {
#pragma HLS INLINE
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (ap_uint<8>)v;
}

ap_uint<8> apply_gain_q8(ap_uint<8> pix, ap_uint<16> gain_q8) {
#pragma HLS INLINE
    int scaled = ((int)pix * (int)gain_q8 + 128) >> 8;
    return clamp_u8(scaled);
}

ap_uint<8> gamma_map(ap_uint<8> pix, ap_uint<2> gamma_mode) {
#pragma HLS INLINE
    const int p = (int)pix;
    int out = p;

    switch ((unsigned)gamma_mode) {
    case 0:
        out = p;
        break;
    case 1:
        // brighten: p + 3/8*(255-p)
        out = p + (((255 - p) * 3) >> 3);
        break;
    case 2:
        // mild darken: 7/8 p
        out = (p * 7) >> 3;
        break;
    default:
        // stronger darken: 3/4 p
        out = (p * 3) >> 2;
        break;
    }
    return clamp_u8(out);
}

void build_wb_gamma_lut(
    ap_uint<16> wb_r_gain,
    ap_uint<16> wb_g_gain,
    ap_uint<16> wb_b_gain,
    ap_uint<2> gamma_mode,
    ap_uint<8> lut_r[256],
    ap_uint<8> lut_g[256],
    ap_uint<8> lut_b[256]) {
#pragma HLS INLINE off
#pragma HLS BIND_STORAGE variable=lut_r type=ram_1p impl=lutram
#pragma HLS BIND_STORAGE variable=lut_g type=ram_1p impl=lutram
#pragma HLS BIND_STORAGE variable=lut_b type=ram_1p impl=lutram

build_lut_loop:
    for (int i = 0; i < 256; ++i) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=256 max=256 avg=256
        ap_uint<8> pix = (ap_uint<8>)i;
        lut_r[i] = gamma_map(apply_gain_q8(pix, wb_r_gain), gamma_mode);
        lut_g[i] = gamma_map(apply_gain_q8(pix, wb_g_gain), gamma_mode);
        lut_b[i] = gamma_map(apply_gain_q8(pix, wb_b_gain), gamma_mode);
    }
}

ap_uint<8> gray_from_rgb(ap_uint<8> r, ap_uint<8> g, ap_uint<8> b) {
#pragma HLS INLINE
    // 0.299R + 0.587G + 0.114B approximation in Q8
    int gray = ((int)r * 77 + (int)g * 150 + (int)b * 29 + 128) >> 8;
    return clamp_u8(gray);
}

ap_uint<8> denoise2tap(ap_uint<8> cur, ap_uint<8> prev, bool enable) {
#pragma HLS INLINE
    if (!enable) {
        return cur;
    }
    return (ap_uint<8>)((((int)cur + (int)prev) + 1) >> 1);
}
