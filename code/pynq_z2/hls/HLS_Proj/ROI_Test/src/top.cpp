#include "isp.h"
#include "roi.h"
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
	ap_uint<16> wb_r_gain,
	ap_uint<16> wb_g_gain,
	ap_uint<16> wb_b_gain,
	ap_uint<8> gamma_mode,
	ap_uint<8> denoise_strength,
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
#pragma HLS INTERFACE s_axilite port=wb_r_gain bundle=control
#pragma HLS INTERFACE s_axilite port=wb_g_gain bundle=control
#pragma HLS INTERFACE s_axilite port=wb_b_gain bundle=control
#pragma HLS INTERFACE s_axilite port=gamma_mode bundle=control
#pragma HLS INTERFACE s_axilite port=denoise_strength bundle=control
#pragma HLS INTERFACE s_axilite port=gray_en bundle=control
#pragma HLS INTERFACE s_axilite port=threshold_en bundle=control
#pragma HLS INTERFACE s_axilite port=threshold_val bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

	RoiWindow roi = pack_roi_window(rows, cols, roi_x, roi_y, roi_w, roi_h);

row_loop:
	for (ap_uint<16> r = 0; r < rows; ++r) {
		// 每行重置历史像素，避免跨行残留导致拖影。
		ap_uint<8> prev_r = 0;
		ap_uint<8> prev_g = 0;
		ap_uint<8> prev_b = 0;
		bool prev_valid = false;
	col_loop:
		for (ap_uint<16> c = 0; c < cols; ++c) {
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

			if (in_roi) {
				// ROI 内 ISP 顺序：白平衡 -> Gamma -> 简易降噪。
				ap_uint<8> isp_r = apply_gain_q8(rr, wb_r_gain);
				ap_uint<8> isp_g = apply_gain_q8(g, wb_g_gain);
				ap_uint<8> isp_b = apply_gain_q8(b, wb_b_gain);

				isp_r = gamma_map(isp_r, gamma_mode);
				isp_g = gamma_map(isp_g, gamma_mode);
				isp_b = gamma_map(isp_b, gamma_mode);

				if (denoise_strength != 0 && prev_valid) {
					isp_r = denoise_mix(isp_r, prev_r, denoise_strength);
					isp_g = denoise_mix(isp_g, prev_g, denoise_strength);
					isp_b = denoise_mix(isp_b, prev_b, denoise_strength);
				}

				prev_r = isp_r;
				prev_g = isp_g;
				prev_b = isp_b;
				prev_valid = true;

				if (gray_en) {
					// 可选灰度/阈值路径，复用 ISP 后结果。
					ap_uint<16> y = (ap_uint<16>)isp_r * 77 + (ap_uint<16>)isp_g * 150 + (ap_uint<16>)isp_b * 29;
					ap_uint<8> gray = (ap_uint<8>)(y >> 8);
					if (threshold_en) {
						ap_uint<8> bin = (gray > threshold_val) ? (ap_uint<8>)255 : (ap_uint<8>)0;
						out_r = bin;
						out_g = bin;
						out_b = bin;
					} else {
						out_r = gray;
						out_g = gray;
						out_b = gray;
					}
				} else {
					out_r = isp_r;
					out_g = isp_g;
					out_b = isp_b;
				}
			} else {
				// ROI 外清零，并清除历史像素有效位，避免跨 ROI 污染。
				prev_valid = false;
			}

			out_pix.data.range(7, 0) = out_b;
			out_pix.data.range(15, 8) = out_g;
			out_pix.data.range(23, 16) = out_r;
			out_pix.keep = in_pix.keep;
			out_pix.strb = in_pix.strb;
			out_pix.user = (r == 0 && c == 0) ? 1 : 0;
			out_pix.last = (c == (cols - 1)) ? 1 : 0;
			out_pix.id = in_pix.id;
			out_pix.dest = in_pix.dest;

			dst.write(out_pix);
		}
	}
}
