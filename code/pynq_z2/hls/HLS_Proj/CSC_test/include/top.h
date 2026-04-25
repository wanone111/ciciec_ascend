#ifndef PYNQ_TOP_H_
#define PYNQ_TOP_H_

#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

#include "video_params.h"

typedef ap_axiu<24, 1, 1, 1> axi_rgb_t;
typedef ap_axiu<8, 1, 1, 1> axi_u8_t;

void video_preprocess(
	hls::stream<axi_rgb_t>& src,
	hls::stream<axi_u8_t>& y_dst,
	hls::stream<axi_u8_t>& uv_dst,
	ap_uint<16> rows,
	ap_uint<16> cols,
	ap_uint<8> input_is_bgr);

#endif  // PYNQ_TOP_H_
