#ifndef PYNQ_ROI_H_
#define PYNQ_ROI_H_

#include <ap_int.h>
#include <stdbool.h>

#include "video_params.h"

struct RoiWindow {
    ap_uint<16> x0;
    ap_uint<16> y0;
    ap_uint<16> x1;
    ap_uint<16> y1;
};

RoiWindow pack_roi_window(ap_uint<16> rows,
                          ap_uint<16> cols,
                          ap_uint<16> roi_x,
                          ap_uint<16> roi_y,
                          ap_uint<16> roi_w,
                          ap_uint<16> roi_h);

bool roi_contains(const RoiWindow& roi, ap_uint<16> row, ap_uint<16> col);

void roi_pack_pixel(ap_uint<8> r,
                    ap_uint<8> g,
                    ap_uint<8> b,
                    bool in_roi,
                    ap_uint<8> gray_en,
                    ap_uint<8> threshold_en,
                    ap_uint<8> threshold_val,
                    ap_uint<8>& out_r,
                    ap_uint<8>& out_g,
                    ap_uint<8>& out_b);

#endif  // PYNQ_ROI_H_
