#ifndef RGB2YUV_H
#define RGB2YUV_H

#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

// AXI-Stream RGB -> NV12 (Y + UV) converter
// Input:  24-bit RGB or BGR stream
// Output: 8-bit Y luma stream + 8-bit UV chroma stream (U,V,U,V...)
typedef ap_axiu<24, 1, 1, 1> axi_rgb_t;
typedef ap_axiu<8, 1, 1, 1> axi_u8_t;

void rgb2yuv420sp_nv12_stream(
	hls::stream<axi_rgb_t>& in_stream,
	hls::stream<axi_u8_t>& y_out,
	hls::stream<axi_u8_t>& uv_out,
	int rows,
	int cols,
	ap_uint<1> input_is_bgr);

#endif // RGB2YUV_H