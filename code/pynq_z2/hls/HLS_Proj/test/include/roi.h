#ifndef ROI_H_
#define ROI_H_

#include <ap_int.h>

struct RoiWindow {
    ap_uint<16> x0;
    ap_uint<16> y0;
    ap_uint<16> x1; // inclusive
    ap_uint<16> y1; // inclusive
    bool valid;
};

RoiWindow pack_roi_window(
    ap_uint<16> rows,
    ap_uint<16> cols,
    ap_uint<16> roi_x,
    ap_uint<16> roi_y,
    ap_uint<16> roi_w,
    ap_uint<16> roi_h);

bool roi_contains(const RoiWindow &roi, ap_uint<16> r, ap_uint<16> c);

#endif // ROI_H_
