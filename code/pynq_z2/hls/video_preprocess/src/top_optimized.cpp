#include "top.h"
#include "isp.h"
#include "roi.h"
#include "rgb2yuv.h"
#include <cstdint>

// ============================================================================
// Unified Video Preprocessing IP: ISP + Dynamic ROI + RGB2YUV
//
// This module integrates three processing stages into a single HLS IP:
// 1. Lightweight ISP: White Balance -> Gamma Correction -> Denoising
// 2. Dynamic ROI: Pixel-level gating based on ROI coordinates
// 3. RGB to YUV420SP (NV12) conversion with 2x2 chroma subsampling
//
// Optimization strategies:
// - TRUE DATAFLOW: independent stages connected via FIFO streams
// - Remove variable division/modulo in pixel loop by using row/column counters
// - Pipeline the pixel loops instead of helper functions
// - Correct 2x2 chroma averaging cache for NV12 UV generation
// - Clamp frame dimensions once at top level so DATAFLOW stages consume the same
//   number of pixels
// ============================================================================

// Internal FIFO payload type.
// Keep AXI packet metadata needed by downstream stage, but avoid using
// ap_axiu/hls::axis for non-top-level streams (required by HLS 2025.2).
struct rgb_fifo_t {
    ap_uint<24> data;
    ap_uint<1> keep;
    ap_uint<1> strb;
    ap_uint<1> user;
    ap_uint<1> last;
    ap_uint<1> id;
    ap_uint<1> dest;
};

static inline ap_uint<16> clamp_rows(ap_uint<16> rows) {
#pragma HLS INLINE
    if (rows == 0) {
        return ap_uint<16>(1);
    }
    if (rows > ap_uint<16>(MAX_HEIGHT)) {
        return ap_uint<16>(MAX_HEIGHT);
    }
    return rows;
}

static inline ap_uint<16> clamp_cols(ap_uint<16> cols) {
#pragma HLS INLINE
    if (cols == 0) {
        return ap_uint<16>(1);
    }
    if (cols > ap_uint<16>(MAX_WIDTH)) {
        return ap_uint<16>(MAX_WIDTH);
    }
    return cols;
}

// ============================================================================
// Stage 1: ISP Processing with ROI gating
// Input: RGB stream, Output: Processed RGB stream with ROI metadata
// ============================================================================
void stage_isp_processing(
    hls::stream<axi_rgb_t>& src,
    hls::stream<rgb_fifo_t>& dst,
    ap_uint<16> rows,
    ap_uint<16> cols,
    RoiWindow roi,
    ap_uint<16> wb_r_gain,
    ap_uint<16> wb_g_gain,
    ap_uint<16> wb_b_gain,
    ap_uint<8> gamma_mode,
    ap_uint<8> denoise_strength,
    ap_uint<8> input_is_bgr)
{
#pragma HLS INLINE OFF

    // Previous pixel values for temporal denoising (within ROI).
    ap_uint<8> prev_denoise_r = 0, prev_denoise_g = 0, prev_denoise_b = 0;
    bool prev_valid = false;

    // Avoid i / cols and i % cols inside the pixel pipeline.
    // Variable division/modulo is expensive in HLS. Row and column are tracked
    // by simple add/compare logic instead.
    int total_pixels = (int)rows * (int)cols;
    ap_uint<16> r = 0;
    ap_uint<16> c = 0;

PIXEL_LOOP:
    for (int i = 0; i < total_pixels; ++i) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=921600  // 1280x720

        // Read input pixel.
        axi_rgb_t in_pix = src.read();
        ap_uint<8> rv, gv, bv;
        unpack_rgb(in_pix.data, input_is_bgr, rv, gv, bv);

        // Check if pixel is inside ROI.
        bool in_roi = roi_contains(roi, r, c);

        // Initialize output values.
        ap_uint<8> out_r = 0, out_g = 0, out_b = 0;

        if (in_roi) {
            // White Balance.
            ap_uint<8> isp_r = apply_gain_q8(rv, wb_r_gain);
            ap_uint<8> isp_g = apply_gain_q8(gv, wb_g_gain);
            ap_uint<8> isp_b = apply_gain_q8(bv, wb_b_gain);

            // Gamma Correction.
            isp_r = gamma_map(isp_r, gamma_mode);
            isp_g = gamma_map(isp_g, gamma_mode);
            isp_b = gamma_map(isp_b, gamma_mode);

            // Simple denoising: weighted blend with previous valid ROI pixel.
            if (denoise_strength != 0 && prev_valid) {
                isp_r = denoise_mix(isp_r, prev_denoise_r, denoise_strength);
                isp_g = denoise_mix(isp_g, prev_denoise_g, denoise_strength);
                isp_b = denoise_mix(isp_b, prev_denoise_b, denoise_strength);
            }

            prev_denoise_r = isp_r;
            prev_denoise_g = isp_g;
            prev_denoise_b = isp_b;
            prev_valid = true;

            out_r = isp_r;
            out_g = isp_g;
            out_b = isp_b;
        } else {
            // Outside ROI: clear output and reset denoise state to avoid dragging
            // color history across ROI boundaries.
            prev_valid = false;
        }

        rgb_fifo_t out_pix;
        out_pix.data.range(23, 16) = out_r;
        out_pix.data.range(15, 8) = out_g;
        out_pix.data.range(7, 0) = out_b;
        out_pix.keep = in_roi ? ap_uint<1>(1) : ap_uint<1>(0);  // ROI flag for internal use.
        out_pix.strb = in_pix.strb;
        out_pix.user = in_pix.user;
        out_pix.last = in_pix.last;
        out_pix.id = in_pix.id;
        out_pix.dest = in_pix.dest;

        dst.write(out_pix);

        // Update row/column counters at the end of each pixel.
        if (c == cols - 1) {
            c = 0;
            r++;
            prev_valid = false;  // Do not mix denoise history across lines.
        } else {
            c++;
        }
    }
}

// ============================================================================
// Stage 2: RGB to YUV Conversion with Chroma Subsampling
// Input: Processed RGB stream, Output: Y stream + UV stream
// ============================================================================
void stage_yuv_conversion(
    hls::stream<rgb_fifo_t>& src,
    hls::stream<axi_u8_t>& y_dst,
    hls::stream<axi_u8_t>& uv_dst,
    ap_uint<16> rows,
    ap_uint<16> cols)
{
#pragma HLS INLINE OFF

    // Line buffers for 2x2 chroma averaging.
    static ap_uint<8> prev_row_r[MAX_WIDTH];
    static ap_uint<8> prev_row_g[MAX_WIDTH];
    static ap_uint<8> prev_row_b[MAX_WIDTH];
#pragma HLS BIND_STORAGE variable=prev_row_r type=ram_2p
#pragma HLS BIND_STORAGE variable=prev_row_g type=ram_2p
#pragma HLS BIND_STORAGE variable=prev_row_b type=ram_2p
#pragma HLS ARRAY_PARTITION variable=prev_row_r cyclic factor=8 dim=1
#pragma HLS ARRAY_PARTITION variable=prev_row_g cyclic factor=8 dim=1
#pragma HLS ARRAY_PARTITION variable=prev_row_b cyclic factor=8 dim=1

    bool uv_user_sent = false;

    // Bottom-left pixel of the current 2x2 block, captured at even columns.
    ap_uint<8> left_r = 0, left_g = 0, left_b = 0;

    // Top-left pixel of the current 2x2 block, captured from the previous row at
    // even columns. This replaces the old top_left update-at-odd-column logic.
    ap_uint<8> top_even_r = 0, top_even_g = 0, top_even_b = 0;

    int proc_rows = (int)rows;
    int proc_cols = (int)cols;
    int even_rows = (proc_rows >> 1) << 1;  // Round down to even.
    int even_cols = (proc_cols >> 1) << 1;  // Round down to even.

ROW_LOOP:
    for (int r = 0; r < proc_rows; ++r) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=720
        if (r == 0) {
            uv_user_sent = false;
        }

COL_LOOP:
        for (int c = 0; c < proc_cols; ++c) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=1280

            // Read previous-row pixels for 2x2 averaging before overwriting the
            // current line-buffer location.
            ap_uint<8> top_r = (r == 0) ? ap_uint<8>(0) : prev_row_r[c];
            ap_uint<8> top_g = (r == 0) ? ap_uint<8>(0) : prev_row_g[c];
            ap_uint<8> top_b = (r == 0) ? ap_uint<8>(0) : prev_row_b[c];

            rgb_fifo_t in_pix = src.read();
            ap_uint<8> out_r = in_pix.data.range(23, 16);
            ap_uint<8> out_g = in_pix.data.range(15, 8);
            ap_uint<8> out_b = in_pix.data.range(7, 0);

            // RGB to YUV conversion for the full-resolution Y plane.
            ap_uint<8> y_val, u_val, v_val;
            calc_yuv_parallel(out_r, out_g, out_b, y_val, u_val, v_val);

            axi_u8_t y_pkt;
            y_pkt.data = y_val;
            y_pkt.keep = 1;
            y_pkt.strb = 1;
            y_pkt.user = in_pix.user;
            y_pkt.last = (c == proc_cols - 1) ? 1 : 0;
            y_pkt.id = in_pix.id;
            y_pkt.dest = in_pix.dest;
            y_dst.write(y_pkt);

            // Cache the left-column pixels of the current 2x2 block. When the
            // next odd column arrives, these values pair with the current pixel.
            if ((c & 1) == 0) {
                top_even_r = top_r;
                top_even_g = top_g;
                top_even_b = top_b;

                left_r = out_r;
                left_g = out_g;
                left_b = out_b;
            }

            // Emit UV for complete 2x2 blocks only. Odd edge row/column is
            // dropped, which is the normal NV12 behavior for odd dimensions.
            if (r < even_rows && c < even_cols &&
                (r & 1) == 1 && (c & 1) == 1) {

                uint16_t avg_r = ((uint16_t)top_even_r + (uint16_t)top_r +
                                  (uint16_t)left_r + (uint16_t)out_r + 2) >> 2;
                uint16_t avg_g = ((uint16_t)top_even_g + (uint16_t)top_g +
                                  (uint16_t)left_g + (uint16_t)out_g + 2) >> 2;
                uint16_t avg_b = ((uint16_t)top_even_b + (uint16_t)top_b +
                                  (uint16_t)left_b + (uint16_t)out_b + 2) >> 2;

                ap_uint<8> uv_y, uv_u, uv_v;
                calc_yuv_parallel((ap_uint<8>)avg_r, (ap_uint<8>)avg_g, (ap_uint<8>)avg_b,
                                  uv_y, uv_u, uv_v);

                axi_u8_t u_pkt;
                u_pkt.data = uv_u;
                u_pkt.keep = 1;
                u_pkt.strb = 1;
                u_pkt.user = uv_user_sent ? 0 : 1;
                u_pkt.last = 0;
                u_pkt.id = in_pix.id;
                u_pkt.dest = in_pix.dest;
                uv_dst.write(u_pkt);
                uv_user_sent = true;

                axi_u8_t v_pkt;
                v_pkt.data = uv_v;
                v_pkt.keep = 1;
                v_pkt.strb = 1;
                v_pkt.user = 0;
                // even_cols is even, so the last valid UV output column is
                // even_cols - 1, not even_cols - 2.
                v_pkt.last = (c == even_cols - 1) ? 1 : 0;
                v_pkt.id = in_pix.id;
                v_pkt.dest = in_pix.dest;
                uv_dst.write(v_pkt);
            }

            // Save current pixel to line buffer for the next row.
            prev_row_r[c] = out_r;
            prev_row_g[c] = out_g;
            prev_row_b[c] = out_b;
        }
    }
}

void video_preprocess(
    hls::stream<axi_rgb_t>& src,
    hls::stream<axi_u8_t>& y_dst,
    hls::stream<axi_u8_t>& uv_dst,
    ap_uint<16> rows,
    ap_uint<16> cols,
    ap_uint<16> roi_x,
    ap_uint<16> roi_y,
    ap_uint<16> roi_w,
    ap_uint<16> roi_h,
    ap_uint<16> wb_r_gain,
    ap_uint<16> wb_g_gain,
    ap_uint<16> wb_b_gain,
    ap_uint<8> gamma_mode,
    ap_uint<8> denoise_strength,
    ap_uint<8> input_is_bgr) {

#pragma HLS INTERFACE axis port=src
#pragma HLS INTERFACE axis port=y_dst
#pragma HLS INTERFACE axis port=uv_dst
#pragma HLS INTERFACE s_axilite port=rows bundle=control
#pragma HLS INTERFACE s_axilite port=cols bundle=control
#pragma HLS INTERFACE s_axilite port=roi_x bundle=control
#pragma HLS INTERFACE s_axilite port=roi_y bundle=control
#pragma HLS INTERFACE s_axilite port=roi_w bundle=control
#pragma HLS INTERFACE s_axilite port=roi_h bundle=control
#pragma HLS INTERFACE s_axilite port=wb_r_gain bundle=control
#pragma HLS INTERFACE s_axilite port=wb_g_gain bundle=control
#pragma HLS INTERFACE s_axilite port=wb_b_gain bundle=control
#pragma HLS INTERFACE s_axilite port=gamma_mode bundle=control
#pragma HLS INTERFACE s_axilite port=denoise_strength bundle=control
#pragma HLS INTERFACE s_axilite port=input_is_bgr bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

#pragma HLS DATAFLOW

    static hls::stream<rgb_fifo_t> isp_to_yuv("isp_to_yuv_fifo");
#pragma HLS STREAM variable=isp_to_yuv depth=64

    // Clamp dimensions once so both DATAFLOW stages read/write the same number
    // of pixels. This avoids producer/consumer mismatch when rows/cols exceed
    // the compile-time supported maximum.
    ap_uint<16> proc_rows = clamp_rows(rows);
    ap_uint<16> proc_cols = clamp_cols(cols);

    RoiWindow roi = pack_roi_window(proc_rows, proc_cols, roi_x, roi_y, roi_w, roi_h);

    stage_isp_processing(
        src,
        isp_to_yuv,
        proc_rows,
        proc_cols,
        roi,
        wb_r_gain,
        wb_g_gain,
        wb_b_gain,
        gamma_mode,
        denoise_strength,
        input_is_bgr
    );

    stage_yuv_conversion(
        isp_to_yuv,
        y_dst,
        uv_dst,
        proc_rows,
        proc_cols
    );
}
