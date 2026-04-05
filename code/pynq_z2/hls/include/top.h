#ifndef TOP_H
#define TOP_H

#include <ap_int.h>

#include "common/xf_common.hpp"
#include "common/xf_utility.hpp"
#include "imgproc/xf_crop.hpp"
#include "imgproc/xf_cvt_color.hpp"
#include "imgproc/xf_threshold.hpp"

#include "video_params.h"

extern "C" {
void vision_preprocess_accel(ap_uint<INPUT_PTR_WIDTH> *img_in,
                             ap_uint<OUTPUT_PTR_WIDTH> *img_out,
                             int rows,
                             int cols,
                             int roi_x,
                             int roi_y,
                             int roi_w,
                             int roi_h,
                             ap_uint<8> thresh_val);
}

#endif
