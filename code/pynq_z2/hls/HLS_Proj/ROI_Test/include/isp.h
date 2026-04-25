#ifndef PYNQ_ISP_H_
#define PYNQ_ISP_H_

#include <ap_int.h>

// Q8 增益：256 表示 1.0x。
ap_uint<8> apply_gain_q8(ap_uint<8> pix, ap_uint<16> gain_q8);

// 离散 gamma 映射：0 旁路，1 提亮，2 轻压暗，3 强压暗。
ap_uint<8> gamma_map(ap_uint<8> x, ap_uint<8> mode);

// 简易降噪：当前像素与历史像素做一阶 IIR 混合。
ap_uint<8> denoise_mix(ap_uint<8> cur, ap_uint<8> prev, ap_uint<8> strength);

#endif  // PYNQ_ISP_H_
