#ifndef PYNQ_VIDEO_PARAMS_H_
#define PYNQ_VIDEO_PARAMS_H_

#include <ap_int.h>

// Compile-time bounds for HLS buffer sizing.
static const int MAX_WIDTH = 1280;
static const int MAX_HEIGHT = 720;

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
