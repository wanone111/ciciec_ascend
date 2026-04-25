#include "isp.h"

#include "video_params.h"

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

static inline ap_uint<8> clamp_u8(ap_uint<16> v) {
#pragma HLS INLINE
	return (v > 255) ? (ap_uint<8>)255 : (ap_uint<8>)v;
}

ap_uint<8> apply_gain_q8(ap_uint<8> pix, ap_uint<16> gain_q8) {
#pragma HLS INLINE
	// Q8 增益：out = in * gain / 256
	ap_uint<24> mult = (ap_uint<24>)pix * (ap_uint<24>)gain_q8;
	ap_uint<16> scaled = (ap_uint<16>)(mult >> 8);
	return clamp_u8(scaled);
}

ap_uint<8> gamma_map(ap_uint<8> x, ap_uint<8> mode) {
#pragma HLS INLINE
	// 轻量 gamma 分段近似：使用离散模式，避免 LUT 和浮点开销。
	if (mode == GAMMA_MODE_BYPASS) {
		return x;
	}
	if (mode == GAMMA_MODE_BRIGHT) {
		if (x < 64) {
			return (ap_uint<8>)(x + (x >> 1));
		}
		if (x < 192) {
			return (ap_uint<8>)(96 + ((x - 64) * 5 >> 2));
		}
		return (ap_uint<8>)(240 + ((x - 192) >> 2));
	}
	if (mode == GAMMA_MODE_DARK) {
		if (x < 128) {
			return (ap_uint<8>)(x >> 1);
		}
		return (ap_uint<8>)(64 + (((x - 128) * 3) >> 2));
	}
	if (x < 128) {
		return (ap_uint<8>)(x >> 2);
	}
	return (ap_uint<8>)(32 + (((x - 128) * 5) >> 3));
}

ap_uint<8> denoise_mix(ap_uint<8> cur, ap_uint<8> prev, ap_uint<8> strength) {
#pragma HLS INLINE
	// 一阶 IIR：当前像素与前一像素加权融合，作为简易去噪。
	ap_uint<9> alpha_prev = strength;
	ap_uint<9> alpha_cur = 256 - alpha_prev;
	ap_uint<18> y = (ap_uint<18>)cur * alpha_cur + (ap_uint<18>)prev * alpha_prev;
	return (ap_uint<8>)(y >> 8);
}
