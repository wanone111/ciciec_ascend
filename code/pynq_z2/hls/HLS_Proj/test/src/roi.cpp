#include "roi.h"

namespace {
static ap_uint<16> clamp_u16_to_range(int v, int lo, int hi) {
#pragma HLS INLINE
    if (v < lo) return (ap_uint<16>)lo;
    if (v > hi) return (ap_uint<16>)hi;
    return (ap_uint<16>)v;
}
}

RoiWindow pack_roi_window(
    ap_uint<16> rows,
    ap_uint<16> cols,
    ap_uint<16> roi_x,
    ap_uint<16> roi_y,
    ap_uint<16> roi_w,
    ap_uint<16> roi_h) {
#pragma HLS INLINE
    RoiWindow roi;
    roi.x0 = 0;
    roi.y0 = 0;
    roi.x1 = 0;
    roi.y1 = 0;
    roi.valid = false;

    if (rows == 0 || cols == 0 || roi_w == 0 || roi_h == 0) {
        return roi;
    }

    const ap_uint<16> max_x = cols - 1;
    const ap_uint<16> max_y = rows - 1;

    ap_uint<16> x0 = clamp_u16_to_range((int)roi_x, 0, (int)max_x);
    ap_uint<16> y0 = clamp_u16_to_range((int)roi_y, 0, (int)max_y);

    int x1_raw = (int)roi_x + (int)roi_w - 1;
    int y1_raw = (int)roi_y + (int)roi_h - 1;

    ap_uint<16> x1 = clamp_u16_to_range(x1_raw, 0, (int)max_x);
    ap_uint<16> y1 = clamp_u16_to_range(y1_raw, 0, (int)max_y);

    if (x1 < x0 || y1 < y0) {
        return roi;
    }

    roi.x0 = x0;
    roi.y0 = y0;
    roi.x1 = x1;
    roi.y1 = y1;
    roi.valid = true;
    return roi;
}

bool roi_contains(const RoiWindow &roi, ap_uint<16> r, ap_uint<16> c) {
#pragma HLS INLINE
    if (!roi.valid) {
        return false;
    }
    return (r >= roi.y0 && r <= roi.y1 && c >= roi.x0 && c <= roi.x1);
}
