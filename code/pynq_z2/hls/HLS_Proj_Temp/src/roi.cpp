#include "roi.h"

static inline ap_uint<16> clamp_u16(ap_uint<16> value, ap_uint<16> lo, ap_uint<16> hi) {
#pragma HLS INLINE
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

RoiWindow pack_roi_window(ap_uint<16> rows,
                          ap_uint<16> cols,
                          ap_uint<16> roi_x,
                          ap_uint<16> roi_y,
                          ap_uint<16> roi_w,
                          ap_uint<16> roi_h) {
    RoiWindow roi;

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

void roi_pack_pixel(ap_uint<8> r,
                    ap_uint<8> g,
                    ap_uint<8> b,
                    bool in_roi,
                    ap_uint<8> gray_en,
                    ap_uint<8> threshold_en,
                    ap_uint<8> threshold_val,
                    ap_uint<8>& out_r,
                    ap_uint<8>& out_g,
                    ap_uint<8>& out_b) {
#pragma HLS INLINE
    out_r = 0;
    out_g = 0;
    out_b = 0;

    if (!in_roi) {
        return;
    }

    if (gray_en) {
        ap_uint<16> y = (ap_uint<16>)r * 77 + (ap_uint<16>)g * 150 + (ap_uint<16>)b * 29;
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
        return;
    }

    out_r = r;
    out_g = g;
    out_b = b;
}
