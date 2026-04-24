#ifndef PYNQ_TOP_H_
#define PYNQ_TOP_H_

#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

#include "video_params.h"

typedef ap_axiu<24, 1, 1, 1> axi_pixel_t;

void video_preprocess(
	hls::stream<axi_pixel_t>& src,
	hls::stream<axi_pixel_t>& dst,
	ap_uint<16> rows,
	ap_uint<16> cols,
	ap_uint<16> roi_x,
	ap_uint<16> roi_y,
	ap_uint<16> roi_w,
	ap_uint<16> roi_h,
	ap_uint<8> gray_en,
	ap_uint<8> threshold_en,
	ap_uint<8> threshold_val);

#endif  // PYNQ_TOP_H_
