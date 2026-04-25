#ifndef PYNQ_ROI_H_
#define PYNQ_ROI_H_

#include <ap_int.h>

#include "video_params.h"

// ROI 窗口边界，采用左上角 + 右下角的闭开区间表示。
struct RoiWindow {
	ap_uint<16> x0;
	ap_uint<16> y0;
	ap_uint<16> x1;
	ap_uint<16> y1;
};

// 将归一化或像素级 ROI 参数整理成可直接判断的窗口边界。
RoiWindow pack_roi_window(ap_uint<16> rows,
						  ap_uint<16> cols,
						  ap_uint<16> roi_x,
						  ap_uint<16> roi_y,
						  ap_uint<16> roi_w,
						  ap_uint<16> roi_h);

// 判断当前像素是否落在 ROI 内。
bool roi_contains(const RoiWindow& roi, ap_uint<16> row, ap_uint<16> col);

#endif  // PYNQ_ROI_H_
