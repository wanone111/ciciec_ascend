#ifndef ISP_H_
#define ISP_H_

#include <ap_int.h>

ap_uint<8> clamp_u8(int v);
ap_uint<8> apply_gain_q8(ap_uint<8> pix, ap_uint<16> gain_q8);
ap_uint<8> gamma_map(ap_uint<8> pix, ap_uint<2> gamma_mode);
void build_wb_gamma_lut(
    ap_uint<16> wb_r_gain,
    ap_uint<16> wb_g_gain,
    ap_uint<16> wb_b_gain,
    ap_uint<2> gamma_mode,
    ap_uint<8> lut_r[256],
    ap_uint<8> lut_g[256],
    ap_uint<8> lut_b[256]);

ap_uint<8> gray_from_rgb(ap_uint<8> r, ap_uint<8> g, ap_uint<8> b);
ap_uint<8> denoise2tap(ap_uint<8> cur, ap_uint<8> prev, bool enable);

#endif // ISP_H_
