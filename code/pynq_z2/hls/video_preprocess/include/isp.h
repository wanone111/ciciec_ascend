#ifndef PYNQ_ISP_H_
#define PYNQ_ISP_H_

#include <ap_int.h>
#include "video_params.h"

// ============================================================================
// ISP Helper Functions - Lightweight inline implementations
// All functions are designed for single-cycle throughput with II=1
// ============================================================================

// Apply Q8 fixed-point gain: out = in * gain / 256
static inline ap_uint<8> apply_gain_q8(ap_uint<8> pix, ap_uint<16> gain_q8) {
#pragma HLS INLINE
    ap_uint<24> mult = (ap_uint<24>)pix * (ap_uint<24>)gain_q8;
    ap_uint<16> scaled = (ap_uint<16>)(mult >> 8);
    return (scaled > 255) ? (ap_uint<8>)255 : (ap_uint<8>)scaled;
}

// Discrete gamma mapping using piecewise linear approximation
// mode: 0=BYPASS, 1=BRIGHT, 2=DARK, 3=DARK_STRONG
static inline ap_uint<8> gamma_map(ap_uint<8> x, ap_uint<8> mode) {
#pragma HLS INLINE
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
    
    // GAMMA_MODE_DARK_STRONG
    if (x < 128) {
        return (ap_uint<8>)(x >> 2);
    }
    return (ap_uint<8>)(32 + (((x - 128) * 5) >> 3));
}

// Simple IIR denoise: weighted blend of current and previous pixel
// strength: 0-255, higher values give more weight to previous pixel
static inline ap_uint<8> denoise_mix(ap_uint<8> cur, ap_uint<8> prev, ap_uint<8> strength) {
#pragma HLS INLINE
    ap_uint<9> alpha_prev = strength;
    ap_uint<9> alpha_cur = 256 - alpha_prev;
    ap_uint<18> y = (ap_uint<18>)cur * alpha_cur + (ap_uint<18>)prev * alpha_prev;
    return (ap_uint<8>)(y >> 8);
}

#endif  // PYNQ_ISP_H_
