#include "roi.h"

static inline ap_uint<16> clamp_u16(ap_uint<16> v, ap_uint<16> lo, ap_uint<16> hi) {
#pragma HLS INLINE
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

RoiWindow pack_roi_window(ap_uint<16> rows,
						  ap_uint<16> cols,
						  ap_uint<16> roi_x,
						  ap_uint<16> roi_y,
						  ap_uint<16> roi_w,
						  ap_uint<16> roi_h) {
	RoiWindow roi;

	// 将 ROI 坐标限制在当前帧有效范围内，避免越界访问。
	ap_uint<16> safe_rows = clamp_u16(rows, 1, MAX_HEIGHT);
	ap_uint<16> safe_cols = clamp_u16(cols, 1, MAX_WIDTH);

	roi.x0 = clamp_u16(roi_x, 0, (safe_cols > 0) ? (ap_uint<16>)(safe_cols - 1) : (ap_uint<16>)0);
	roi.y0 = clamp_u16(roi_y, 0, (safe_rows > 0) ? (ap_uint<16>)(safe_rows - 1) : (ap_uint<16>)0);

	ap_uint<16> w = (roi_w == 0) ? safe_cols : roi_w;
	ap_uint<16> h = (roi_h == 0) ? safe_rows : roi_h;

	roi.x1 = roi.x0 + w;
	roi.y1 = roi.y0 + h;
	if (roi.x1 > safe_cols) {
		roi.x1 = safe_cols;
	}
	if (roi.y1 > safe_rows) {
		roi.y1 = safe_rows;
	}

	return roi;
}

bool roi_contains(const RoiWindow& roi, ap_uint<16> row, ap_uint<16> col) {
	return (row >= roi.y0) && (row < roi.y1) && (col >= roi.x0) && (col < roi.x1);
}
