#include <ap_int.h>

#include "common/xf_common.hpp"
#include "common/xf_utility.hpp"
#include "imgproc/xf_cvt_color.hpp"
#include "imgproc/xf_threshold.hpp"

#define HEIGHT 720
#define WIDTH 1280

#define NPPCX XF_NPPC1

#define IN_TYPE XF_8UC3
#define OUT_TYPE XF_8UC1

#define INPUT_PTR_WIDTH 32
#define OUTPUT_PTR_WIDTH 8

#define XF_CV_DEPTH_IN 2
#define XF_CV_DEPTH_GRAY 2
#define XF_CV_DEPTH_OUT 2

extern "C" {
void vision_pre(ap_uint<INPUT_PTR_WIDTH>* img_in,
                ap_uint<OUTPUT_PTR_WIDTH>* img_out,
                int rows,
                int cols,
                int size,
                ap_uint<8> thresh_val) {
#pragma HLS INTERFACE m_axi port=img_in offset=slave bundle=gmem0 depth=4096
#pragma HLS INTERFACE m_axi port=img_out offset=slave bundle=gmem1 depth=4096
#pragma HLS INTERFACE s_axilite port=img_in bundle=control
#pragma HLS INTERFACE s_axilite port=img_out bundle=control
#pragma HLS INTERFACE s_axilite port=rows bundle=control
#pragma HLS INTERFACE s_axilite port=cols bundle=control
#pragma HLS INTERFACE s_axilite port=size bundle=control
#pragma HLS INTERFACE s_axilite port=thresh_val bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    xf::cv::Mat<IN_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_IN> in_mat(rows, cols);
    xf::cv::Mat<OUT_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_GRAY> gray_mat(rows, cols);
    xf::cv::Mat<OUT_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_OUT> out_mat(rows, cols);

#pragma HLS DATAFLOW

    xf::cv::Array2xfMat<INPUT_PTR_WIDTH, IN_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_IN>(img_in, in_mat);

    xf::cv::bgr2gray<IN_TYPE, OUT_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_IN, XF_CV_DEPTH_GRAY>(in_mat, gray_mat);

    xf::cv::Threshold<XF_THRESHOLD_TYPE_BINARY, OUT_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_GRAY, XF_CV_DEPTH_OUT>(
        gray_mat, out_mat, (short)thresh_val, (short)255);

    xf::cv::xfMat2Array<OUTPUT_PTR_WIDTH, OUT_TYPE, HEIGHT, WIDTH, NPPCX, XF_CV_DEPTH_OUT>(out_mat, img_out);

    (void)size;
}
}
