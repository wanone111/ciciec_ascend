#include "top.h"

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
	ap_uint<8> threshold_val) {
#pragma HLS INTERFACE axis port=src
#pragma HLS INTERFACE axis port=dst
#pragma HLS INTERFACE s_axilite port=rows bundle=control
#pragma HLS INTERFACE s_axilite port=cols bundle=control
#pragma HLS INTERFACE s_axilite port=roi_x bundle=control
#pragma HLS INTERFACE s_axilite port=roi_y bundle=control
#pragma HLS INTERFACE s_axilite port=roi_w bundle=control
#pragma HLS INTERFACE s_axilite port=roi_h bundle=control
#pragma HLS INTERFACE s_axilite port=gray_en bundle=control
#pragma HLS INTERFACE s_axilite port=threshold_en bundle=control
#pragma HLS INTERFACE s_axilite port=threshold_val bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

	ap_uint<16> proc_rows = rows;
	ap_uint<16> proc_cols = cols;
	RoiWindow roi = pack_roi_window(rows, cols, roi_x, roi_y, roi_w, roi_h);

row_loop:
	for (ap_uint<16> r = 0; r < proc_rows; ++r) {
	col_loop:
		for (ap_uint<16> c = 0; c < proc_cols; ++c) {
#pragma HLS PIPELINE II=1
			axi_pixel_t in_pix = src.read();
			axi_pixel_t out_pix;

			ap_uint<8> b = in_pix.data.range(7, 0);
			ap_uint<8> g = in_pix.data.range(15, 8);
			ap_uint<8> rr = in_pix.data.range(23, 16);

			bool in_roi = roi_contains(roi, r, c);

			ap_uint<8> out_r = 0;
			ap_uint<8> out_g = 0;
			ap_uint<8> out_b = 0;
			roi_pack_pixel(rr, g, b, in_roi, gray_en, threshold_en, threshold_val, out_r, out_g, out_b);

			out_pix.data.range(7, 0) = out_b;
			out_pix.data.range(15, 8) = out_g;
			out_pix.data.range(23, 16) = out_r;
			out_pix.keep = in_pix.keep;
			out_pix.strb = in_pix.strb;
			out_pix.user = (r == 0 && c == 0) ? 1 : 0;
			out_pix.last = (c == (proc_cols - 1)) ? 1 : 0;
			out_pix.id = in_pix.id;
			out_pix.dest = in_pix.dest;

			dst.write(out_pix);
		}
	}
}
