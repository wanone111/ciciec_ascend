#ifndef TOP_H_
#define TOP_H_

#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

#include "video_params.h"

typedef ap_axiu<24, 1, 1, 1> axis_rgb_t;

void video_preprocess(
    hls::stream<axis_rgb_t> &src,
    hls::stream<axis_rgb_t> &dst,
    ap_uint<16> rows,
    ap_uint<16> cols,
    ap_uint<16> roi_x,
    ap_uint<16> roi_y,
    ap_uint<16> roi_w,
    ap_uint<16> roi_h,
    ap_uint<2>  proc_mode,
    ap_uint<8>  threshold_val,
    ap_uint<16> wb_r_gain,
    ap_uint<16> wb_g_gain,
    ap_uint<16> wb_b_gain,
    ap_uint<2>  gamma_mode,
    ap_uint<8>  denoise_strength,
    ap_uint<2>  roi_bg_mode);

#endif // TOP_H_
