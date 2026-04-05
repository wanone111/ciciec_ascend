#include "top.h"

extern "C" {
void vision_preprocess_accel(ap_uint<INPUT_PTR_WIDTH> *img_in,
							 ap_uint<OUTPUT_PTR_WIDTH> *img_out,
							 int rows,
							 int cols,
						 int roi_x,
						 int roi_y,
						 int roi_w,
						 int roi_h,
							 ap_uint<8> thresh_val) {
#pragma HLS INTERFACE m_axi port = img_in offset = slave bundle = gmem0 depth = 4096
#pragma HLS INTERFACE m_axi port = img_out offset = slave bundle = gmem1 depth = 4096

#pragma HLS INTERFACE s_axilite port = img_in bundle = control
#pragma HLS INTERFACE s_axilite port = img_out bundle = control
#pragma HLS INTERFACE s_axilite port = rows bundle = control
#pragma HLS INTERFACE s_axilite port = cols bundle = control
#pragma HLS INTERFACE s_axilite port = roi_x bundle = control
#pragma HLS INTERFACE s_axilite port = roi_y bundle = control
#pragma HLS INTERFACE s_axilite port = roi_w bundle = control
#pragma HLS INTERFACE s_axilite port = roi_h bundle = control
#pragma HLS INTERFACE s_axilite port = thresh_val bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

	int aligned_x = roi_x & ~1;
	int aligned_y = roi_y & ~1;
	int aligned_w = roi_w & ~1;
	int aligned_h = roi_h & ~1;

	if (aligned_x < 0) aligned_x = 0;
	if (aligned_y < 0) aligned_y = 0;
	if (aligned_w < 2) aligned_w = 2;
	if (aligned_h < 2) aligned_h = 2;
	if (aligned_x + aligned_w > cols) {
		aligned_w = cols - aligned_x;
	}
	if (aligned_y + aligned_h > rows) {
		aligned_h = rows - aligned_y;
	}
	if (aligned_w & 1) {
		--aligned_w;
	}
	if (aligned_h & 1) {
		--aligned_h;
	}
	if (aligned_w < 2) aligned_w = 2;
	if (aligned_h < 2) aligned_h = 2;
	for (int r = 0; r < aligned_h; ++r) {
#pragma HLS LOOP_TRIPCOUNT min = 2 max = MAX_HEIGHT
		for (int c = 0; c < aligned_w; ++c) {
#pragma HLS LOOP_TRIPCOUNT min = 2 max = MAX_WIDTH
#pragma HLS PIPELINE II = 1
			int src_idx = (aligned_y + r) * cols + (aligned_x + c);
			ap_uint<INPUT_PTR_WIDTH> pix = img_in[src_idx];
			ap_uint<8> b = pix.range(7, 0);
			ap_uint<8> g = pix.range(15, 8);
			ap_uint<8> red = pix.range(23, 16);
			ap_uint<16> gray = (ap_uint<16>)red * 77 + (ap_uint<16>)g * 150 + (ap_uint<16>)b * 29;
			gray >>= 8;
			img_out[r * aligned_w + c] = (gray > (ap_uint<16>)thresh_val) ? 255 : 0;
		}
	}
}
}

