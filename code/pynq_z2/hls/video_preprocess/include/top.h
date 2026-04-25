#ifndef PYNQ_TOP_H_
#define PYNQ_TOP_H_

#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

#include "video_params.h"

// ============================================================================
// AXI4-Stream Data Types
// ============================================================================
typedef ap_axiu<24, 1, 1, 1> axi_rgb_t;    // RGB/BGR 24-bit input
typedef ap_axiu<8, 1, 1, 1> axi_u8_t;      // Y or UV 8-bit output

// ============================================================================
// Unified Video Preprocessing IP
// 
// Pipeline: ISP (White Balance + Gamma + Denoise) -> Dynamic ROI -> RGB2YUV
// 
// Input: RGB/BGR video stream via AXI4-Stream
// Output: YUV420SP (NV12) video stream via dual AXI4-Stream (Y + UV interleaved)
// 
// Features:
// - Lightweight closed-loop ISP: white balance, gamma correction, denoising
// - Dynamic ROI cropping guided by motion significance (external control)
// - RGB to YUV420SP conversion with 128-byte alignment for UDP transmission
// ============================================================================
void video_preprocess(
    hls::stream<axi_rgb_t>& src,      // Input RGB video stream
    hls::stream<axi_u8_t>& y_dst,     // Output Y (luma) stream
    hls::stream<axi_u8_t>& uv_dst,    // Output UV (chroma interleaved) stream
    ap_uint<16> rows,                 // Frame height in pixels
    ap_uint<16> cols,                 // Frame width in pixels
    ap_uint<16> roi_x,                // ROI top-left X coordinate
    ap_uint<16> roi_y,                // ROI top-left Y coordinate
    ap_uint<16> roi_w,                // ROI width in pixels
    ap_uint<16> roi_h,                // ROI height in pixels
    ap_uint<16> wb_r_gain,            // White balance R gain (Q8: 256=1.0x)
    ap_uint<16> wb_g_gain,            // White balance G gain (Q8: 256=1.0x)
    ap_uint<16> wb_b_gain,            // White balance B gain (Q8: 256=1.0x)
    ap_uint<8> gamma_mode,            // Gamma mode: 0=BYPASS, 1=BRIGHT, 2=DARK, 3=DARK_STRONG
    ap_uint<8> denoise_strength,      // Denoise strength: 0-255
    ap_uint<8> input_is_bgr           // Input format: 1=BGR, 0=RGB
);

#endif  // PYNQ_TOP_H_
