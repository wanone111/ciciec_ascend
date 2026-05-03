#include "top.h"
#include "isp.h"
#include "roi.h"
#include "rgb2yuv_optimized.h"
#include <cstdint>

// ============================================================================
// 60FPS-oriented Video Preprocess IP
//
// Design goal:
//   1280x720 @ 60fps needs ~55.3 Mpixel/s.
//   At 100 MHz, the pixel path should target II=1.
//
// Main ideas in this version:
//   1. Keep the original external interface and functions.
//   2. Split heavy ISP work into dataflow stages.
//   3. Remove variable division/modulo from pixel loops.
//   4. Keep denoise function, but use shift-based strength bins instead of
//      a full multiply-add feedback path.
//   5. Make the YUV stage more HLS-friendly:
//      - even rows write the chroma line buffer
//      - odd rows read the chroma line buffer and emit UV
//      - calculate Y-only for every pixel, and UV-only for 2x2 blocks
//   6. Target II=1 on pixel loops. If timing still fails, check csynth.rpt;
//      the next step is to add one more register stage to the reported bottleneck.
// ============================================================================

struct rgb_fifo_t {
    ap_uint<24> data;
    ap_uint<1>  keep;  // internal ROI-valid flag
    ap_uint<1>  strb;
    ap_uint<1>  user;
    ap_uint<1>  last;
    ap_uint<1>  id;
    ap_uint<1>  dest;
};

static inline ap_uint<16> clamp_rows(ap_uint<16> rows) {
#pragma HLS INLINE
    if (rows == 0) return ap_uint<16>(1);
    if (rows > ap_uint<16>(MAX_HEIGHT)) return ap_uint<16>(MAX_HEIGHT);
    return rows;
}

static inline ap_uint<16> clamp_cols(ap_uint<16> cols) {
#pragma HLS INLINE
    if (cols == 0) return ap_uint<16>(1);
    if (cols > ap_uint<16>(MAX_WIDTH)) return ap_uint<16>(MAX_WIDTH);
    return cols;
}

// Y-only conversion. This avoids wasting U/V hardware for the full-resolution Y plane.
static inline ap_uint<8> calc_y_only(ap_uint<8> r, ap_uint<8> g, ap_uint<8> b) {
#pragma HLS INLINE
    int32_t y_acc = CSC_Y_R_COEF * (int32_t)r +
                    CSC_Y_G_COEF * (int32_t)g +
                    CSC_Y_B_COEF * (int32_t)b + CSC_Y_OFFSET;
    return saturate((y_acc + 128) >> 8);
}

// UV-only conversion for the 2x2 averaged chroma samples.
static inline void calc_uv_only(ap_uint<8> r, ap_uint<8> g, ap_uint<8> b,
                                ap_uint<8>& u_out, ap_uint<8>& v_out) {
#pragma HLS INLINE
    int32_t u_acc = CSC_U_R_COEF * (int32_t)r +
                    CSC_U_G_COEF * (int32_t)g +
                    CSC_U_B_COEF * (int32_t)b + CSC_U_OFFSET;
    u_out = saturate((u_acc + 128) >> 8);

    int32_t v_acc = CSC_V_R_COEF * (int32_t)r +
                    CSC_V_G_COEF * (int32_t)g +
                    CSC_V_B_COEF * (int32_t)b + CSC_V_OFFSET;
    v_out = saturate((v_acc + 128) >> 8);
}

// Shift-based denoise approximation.
// Keeps denoise functionality while avoiding the long cur*alpha + prev*alpha path.
static inline ap_uint<8> denoise_mix_shift(ap_uint<8> cur,
                                           ap_uint<8> prev,
                                           ap_uint<8> strength) {
#pragma HLS INLINE
    ap_uint<10> c = cur;
    ap_uint<10> p = prev;

    if (strength == 0) {
        return cur;
    }

    if (strength < 96) {
        // ~25% previous, 75% current: (3*cur + prev) / 4
        return (ap_uint<8>)(((c << 1) + c + p + 2) >> 2);
    }

    if (strength < 160) {
        // ~50% previous, 50% current
        return (ap_uint<8>)((c + p + 1) >> 1);
    }

    // ~75% previous, 25% current: (cur + 3*prev) / 4
    return (ap_uint<8>)((c + (p << 1) + p + 2) >> 2);
}

// Compile-time gamma variants. This removes runtime gamma_mode comparisons
// from the pixel loop.
template<int MODE>
static inline ap_uint<8> gamma_map_const(ap_uint<8> x) {
#pragma HLS INLINE
    return x;
}

template<>
inline ap_uint<8> gamma_map_const<1>(ap_uint<8> x) {
#pragma HLS INLINE
    if (x < 64) {
        return (ap_uint<8>)(x + (x >> 1));
    }
    if (x < 192) {
        return (ap_uint<8>)(96 + ((x - 64) * 5 >> 2));
    }
    return (ap_uint<8>)(240 + ((x - 192) >> 2));
}

template<>
inline ap_uint<8> gamma_map_const<2>(ap_uint<8> x) {
#pragma HLS INLINE
    if (x < 128) {
        return (ap_uint<8>)(x >> 1);
    }
    return (ap_uint<8>)(64 + (((x - 128) * 3) >> 2));
}

template<>
inline ap_uint<8> gamma_map_const<3>(ap_uint<8> x) {
#pragma HLS INLINE
    if (x < 128) {
        return (ap_uint<8>)(x >> 2);
    }
    return (ap_uint<8>)(32 + (((x - 128) * 5) >> 3));
}

// ============================================================================
// Stage 1: ROI + White Balance
// ============================================================================
void stage_roi_wb_processing(
    hls::stream<axi_rgb_t>& src,
    hls::stream<rgb_fifo_t>& dst,
    ap_uint<16> rows,
    ap_uint<16> cols,
    RoiWindow roi,
    ap_uint<16> wb_r_gain,
    ap_uint<16> wb_g_gain,
    ap_uint<16> wb_b_gain,
    ap_uint<8> input_is_bgr)
{
#pragma HLS INLINE OFF

    int total_pixels = (int)rows * (int)cols;
    ap_uint<16> r = 0;
    ap_uint<16> c = 0;

PIXEL_LOOP_ROI_WB:
    for (int i = 0; i < total_pixels; ++i) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=921600

        axi_rgb_t in_pix = src.read();

        ap_uint<8> rv, gv, bv;
        unpack_rgb(in_pix.data, input_is_bgr, rv, gv, bv);

        bool in_roi = roi_contains(roi, r, c);

        ap_uint<8> out_r = 0;
        ap_uint<8> out_g = 0;
        ap_uint<8> out_b = 0;

        if (in_roi) {
            out_r = apply_gain_q8(rv, wb_r_gain);
            out_g = apply_gain_q8(gv, wb_g_gain);
            out_b = apply_gain_q8(bv, wb_b_gain);
        }

        rgb_fifo_t out_pix;
        out_pix.data.range(23, 16) = out_r;
        out_pix.data.range(15, 8)  = out_g;
        out_pix.data.range(7, 0)   = out_b;
        out_pix.keep = in_roi ? ap_uint<1>(1) : ap_uint<1>(0);
        out_pix.strb = in_pix.strb;
        out_pix.user = in_pix.user;
        out_pix.last = in_pix.last;
        out_pix.id   = in_pix.id;
        out_pix.dest = in_pix.dest;
        dst.write(out_pix);

        if (c == cols - 1) {
            c = 0;
            r++;
        } else {
            c++;
        }
    }
}

// ============================================================================
// Stage 2: Gamma correction, mode-specialized loop
// ============================================================================
template<int MODE>
void stage_gamma_mode(
    hls::stream<rgb_fifo_t>& src,
    hls::stream<rgb_fifo_t>& dst,
    ap_uint<16> rows,
    ap_uint<16> cols)
{
#pragma HLS INLINE OFF
    int total_pixels = (int)rows * (int)cols;

PIXEL_LOOP_GAMMA_MODE:
    for (int i = 0; i < total_pixels; ++i) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=921600

        rgb_fifo_t in_pix = src.read();
        rgb_fifo_t out_pix = in_pix;

        if (in_pix.keep) {
            ap_uint<8> r = in_pix.data.range(23, 16);
            ap_uint<8> g = in_pix.data.range(15, 8);
            ap_uint<8> b = in_pix.data.range(7, 0);

            out_pix.data.range(23, 16) = gamma_map_const<MODE>(r);
            out_pix.data.range(15, 8)  = gamma_map_const<MODE>(g);
            out_pix.data.range(7, 0)   = gamma_map_const<MODE>(b);
        }

        dst.write(out_pix);
    }
}

void stage_gamma_processing(
    hls::stream<rgb_fifo_t>& src,
    hls::stream<rgb_fifo_t>& dst,
    ap_uint<16> rows,
    ap_uint<16> cols,
    ap_uint<8> gamma_mode)
{
#pragma HLS INLINE OFF
    if (gamma_mode == GAMMA_MODE_BYPASS) {
        stage_gamma_mode<0>(src, dst, rows, cols);
    } else if (gamma_mode == GAMMA_MODE_BRIGHT) {
        stage_gamma_mode<1>(src, dst, rows, cols);
    } else if (gamma_mode == GAMMA_MODE_DARK) {
        stage_gamma_mode<2>(src, dst, rows, cols);
    } else {
        stage_gamma_mode<3>(src, dst, rows, cols);
    }
}

// ============================================================================
// ============================================================================
// Stage 3: realtime-safe temporal denoise
//
// v8 change:
//   v7 timing was blocked by denoise active modes because the loop had a
//   filtered-output feedback path:
//       previous filtered pixel -> mix -> next previous filtered pixel
//
//   This version keeps an optional temporal smoothing mode, but updates history
//   with the previous raw input pixel instead of the previous filtered output.
//   The active path is only one add + shift per channel:
//       out = (current + previous_raw) / 2
//
//   denoise_strength semantics in this realtime version:
//       0      : bypass
//       nonzero: lightweight 50/50 temporal smoothing
// ============================================================================
void stage_denoise_bypass(
    hls::stream<rgb_fifo_t>& src,
    hls::stream<rgb_fifo_t>& dst,
    ap_uint<16> rows,
    ap_uint<16> cols)
{
#pragma HLS INLINE OFF
    int total_pixels = (int)rows * (int)cols;

PIXEL_LOOP_DENOISE_BYPASS:
    for (int i = 0; i < total_pixels; ++i) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=921600
        dst.write(src.read());
    }
}

static inline ap_uint<8> denoise_mix_fast(ap_uint<8> cur, ap_uint<8> prev) {
#pragma HLS INLINE
    ap_uint<9> sum = (ap_uint<9>)cur + (ap_uint<9>)prev + 1;
    return (ap_uint<8>)(sum >> 1);
}

void stage_denoise_active_fast(
    hls::stream<rgb_fifo_t>& src,
    hls::stream<rgb_fifo_t>& dst,
    ap_uint<16> rows,
    ap_uint<16> cols)
{
#pragma HLS INLINE OFF
    int total_pixels = (int)rows * (int)cols;

    ap_uint<8> prev_in_r = 0;
    ap_uint<8> prev_in_g = 0;
    ap_uint<8> prev_in_b = 0;
    bool prev_valid = false;

PIXEL_LOOP_DENOISE_ACTIVE_FAST:
    for (int i = 0; i < total_pixels; ++i) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=921600

        rgb_fifo_t in_pix = src.read();
        rgb_fifo_t out_pix = in_pix;

        if (in_pix.keep) {
            ap_uint<8> raw_r = in_pix.data.range(23, 16);
            ap_uint<8> raw_g = in_pix.data.range(15, 8);
            ap_uint<8> raw_b = in_pix.data.range(7, 0);

            ap_uint<8> out_r = raw_r;
            ap_uint<8> out_g = raw_g;
            ap_uint<8> out_b = raw_b;

            if (prev_valid) {
                out_r = denoise_mix_fast(raw_r, prev_in_r);
                out_g = denoise_mix_fast(raw_g, prev_in_g);
                out_b = denoise_mix_fast(raw_b, prev_in_b);
            }

            // Update history with raw input, not filtered output. This removes
            // the long filtered-output feedback path from the timing-critical loop.
            prev_in_r = raw_r;
            prev_in_g = raw_g;
            prev_in_b = raw_b;
            prev_valid = true;

            out_pix.data.range(23, 16) = out_r;
            out_pix.data.range(15, 8)  = out_g;
            out_pix.data.range(7, 0)   = out_b;
        } else {
            prev_valid = false;
        }

        if (in_pix.last) {
            prev_valid = false;
        }

        dst.write(out_pix);
    }
}

void stage_denoise_processing(
    hls::stream<rgb_fifo_t>& src,
    hls::stream<rgb_fifo_t>& dst,
    ap_uint<16> rows,
    ap_uint<16> cols,
    ap_uint<8> denoise_strength)
{
#pragma HLS INLINE OFF
    if (denoise_strength == 0) {
        stage_denoise_bypass(src, dst, rows, cols);
    } else {
        stage_denoise_active_fast(src, dst, rows, cols);
    }
}

// ============================================================================
// Stage 4 helpers: YUV even/odd row processing
//
// v7 change:
//   The odd row is processed in 2-pixel groups. The loop may report II=2, but
//   each iteration consumes 2 pixels and emits 2 Y samples + one UV pair, so the
//   effective throughput is still 1 pixel/clock. The top line buffers are cyclic
//   partitioned by 2 so the even/odd top pixels of a 2x2 block can be read in
//   parallel from different banks.
// ============================================================================
void yuv_even_row_loop(
    hls::stream<rgb_fifo_t>& src,
    hls::stream<axi_u8_t>& y_dst,
    ap_uint<8> even_row_r[MAX_WIDTH],
    ap_uint<8> even_row_g[MAX_WIDTH],
    ap_uint<8> even_row_b[MAX_WIDTH],
    int proc_cols,
    int even_cols)
{
#pragma HLS INLINE OFF

EVEN_COL_LOOP_YUV:
    for (int c = 0; c < proc_cols; ++c) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=1280

        rgb_fifo_t in_pix = src.read();
        ap_uint<8> cur_r = in_pix.data.range(23, 16);
        ap_uint<8> cur_g = in_pix.data.range(15, 8);
        ap_uint<8> cur_b = in_pix.data.range(7, 0);

        axi_u8_t y_pkt;
        y_pkt.data = calc_y_only(cur_r, cur_g, cur_b);
        y_pkt.keep = 1;
        y_pkt.strb = 1;
        y_pkt.user = in_pix.user;
        y_pkt.last = (c == proc_cols - 1) ? 1 : 0;
        y_pkt.id   = in_pix.id;
        y_pkt.dest = in_pix.dest;
        y_dst.write(y_pkt);

        if (c < even_cols) {
            even_row_r[c] = cur_r;
            even_row_g[c] = cur_g;
            even_row_b[c] = cur_b;
        }
    }
}

void yuv_odd_row_loop_pair2(
    hls::stream<rgb_fifo_t>& src,
    hls::stream<axi_u8_t>& y_dst,
    hls::stream<axi_u8_t>& uv_dst,
    ap_uint<8> even_row_r[MAX_WIDTH],
    ap_uint<8> even_row_g[MAX_WIDTH],
    ap_uint<8> even_row_b[MAX_WIDTH],
    int proc_cols,
    int even_cols,
    bool& uv_user_sent)
{
#pragma HLS INLINE OFF

    int pair_count = even_cols >> 1;

ODD_PAIR_LOOP_YUV:
    for (int k = 0; k < pair_count; ++k) {
#pragma HLS PIPELINE II=2
#pragma HLS LOOP_TRIPCOUNT min=1 max=640

        int c0 = k << 1;
        int c1 = c0 + 1;

        // Read two adjacent pixels from the current odd row.
        rgb_fifo_t pix0 = src.read();
        rgb_fifo_t pix1 = src.read();

        ap_uint<8> cur0_r = pix0.data.range(23, 16);
        ap_uint<8> cur0_g = pix0.data.range(15, 8);
        ap_uint<8> cur0_b = pix0.data.range(7, 0);
        ap_uint<8> cur1_r = pix1.data.range(23, 16);
        ap_uint<8> cur1_g = pix1.data.range(15, 8);
        ap_uint<8> cur1_b = pix1.data.range(7, 0);

        // Output Y for both pixels. Two writes per iteration are intentional;
        // II=2 still means one Y pixel per clock on average.
        axi_u8_t y0;
        y0.data = calc_y_only(cur0_r, cur0_g, cur0_b);
        y0.keep = 1;
        y0.strb = 1;
        y0.user = pix0.user;
        y0.last = 0;
        y0.id   = pix0.id;
        y0.dest = pix0.dest;
        y_dst.write(y0);

        axi_u8_t y1;
        y1.data = calc_y_only(cur1_r, cur1_g, cur1_b);
        y1.keep = 1;
        y1.strb = 1;
        y1.user = pix1.user;
        y1.last = (c1 == proc_cols - 1) ? 1 : 0;
        y1.id   = pix1.id;
        y1.dest = pix1.dest;
        y_dst.write(y1);

        // Read top-left/top-right from the previous even row. The buffers are
        // partitioned by 2, so c0 and c1 are in different banks.
        ap_uint<8> top0_r = even_row_r[c0];
        ap_uint<8> top0_g = even_row_g[c0];
        ap_uint<8> top0_b = even_row_b[c0];
        ap_uint<8> top1_r = even_row_r[c1];
        ap_uint<8> top1_g = even_row_g[c1];
        ap_uint<8> top1_b = even_row_b[c1];

        uint16_t avg_r = ((uint16_t)top0_r + (uint16_t)top1_r +
                          (uint16_t)cur0_r + (uint16_t)cur1_r + 2) >> 2;
        uint16_t avg_g = ((uint16_t)top0_g + (uint16_t)top1_g +
                          (uint16_t)cur0_g + (uint16_t)cur1_g + 2) >> 2;
        uint16_t avg_b = ((uint16_t)top0_b + (uint16_t)top1_b +
                          (uint16_t)cur0_b + (uint16_t)cur1_b + 2) >> 2;

        ap_uint<8> uv_u, uv_v;
        calc_uv_only((ap_uint<8>)avg_r, (ap_uint<8>)avg_g, (ap_uint<8>)avg_b,
                     uv_u, uv_v);

        axi_u8_t u_pkt;
        u_pkt.data = uv_u;
        u_pkt.keep = 1;
        u_pkt.strb = 1;
        u_pkt.user = uv_user_sent ? 0 : 1;
        u_pkt.last = 0;
        u_pkt.id   = pix1.id;
        u_pkt.dest = pix1.dest;
        uv_dst.write(u_pkt);
        uv_user_sent = true;

        axi_u8_t v_pkt;
        v_pkt.data = uv_v;
        v_pkt.keep = 1;
        v_pkt.strb = 1;
        v_pkt.user = 0;
        v_pkt.last = (c1 == even_cols - 1) ? 1 : 0;
        v_pkt.id   = pix1.id;
        v_pkt.dest = pix1.dest;
        uv_dst.write(v_pkt);
    }

    // If the frame width is odd, one extra pixel remains on the odd row. It has
    // a Y sample but no UV sample.
    if (proc_cols > even_cols) {
        rgb_fifo_t tail = src.read();
        ap_uint<8> tr = tail.data.range(23, 16);
        ap_uint<8> tg = tail.data.range(15, 8);
        ap_uint<8> tb = tail.data.range(7, 0);

        axi_u8_t y_tail;
        y_tail.data = calc_y_only(tr, tg, tb);
        y_tail.keep = 1;
        y_tail.strb = 1;
        y_tail.user = tail.user;
        y_tail.last = 1;
        y_tail.id   = tail.id;
        y_tail.dest = tail.dest;
        y_dst.write(y_tail);
    }
}

// ============================================================================
// Stage 4: RGB to YUV420SP / NV12 conversion
// ============================================================================
void stage_yuv_conversion(
    hls::stream<rgb_fifo_t>& src,
    hls::stream<axi_u8_t>& y_dst,
    hls::stream<axi_u8_t>& uv_dst,
    ap_uint<16> rows,
    ap_uint<16> cols)
{
#pragma HLS INLINE OFF

    static ap_uint<8> even_row_r[MAX_WIDTH];
    static ap_uint<8> even_row_g[MAX_WIDTH];
    static ap_uint<8> even_row_b[MAX_WIDTH];
#pragma HLS BIND_STORAGE variable=even_row_r type=ram_2p
#pragma HLS BIND_STORAGE variable=even_row_g type=ram_2p
#pragma HLS BIND_STORAGE variable=even_row_b type=ram_2p
#pragma HLS ARRAY_PARTITION variable=even_row_r cyclic factor=2 dim=1
#pragma HLS ARRAY_PARTITION variable=even_row_g cyclic factor=2 dim=1
#pragma HLS ARRAY_PARTITION variable=even_row_b cyclic factor=2 dim=1
#pragma HLS DEPENDENCE variable=even_row_r inter false
#pragma HLS DEPENDENCE variable=even_row_g inter false
#pragma HLS DEPENDENCE variable=even_row_b inter false

    int proc_rows = (int)rows;
    int proc_cols = (int)cols;
    int even_cols = (proc_cols >> 1) << 1;

    bool uv_user_sent = false;

ROW_LOOP_YUV:
    for (int r = 0; r < proc_rows; ++r) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=720
        if (r == 0) {
            uv_user_sent = false;
        }

        if ((r & 1) == 0) {
            yuv_even_row_loop(src, y_dst,
                              even_row_r, even_row_g, even_row_b,
                              proc_cols, even_cols);
        } else {
            yuv_odd_row_loop_pair2(src, y_dst, uv_dst,
                                   even_row_r, even_row_g, even_row_b,
                                   proc_cols, even_cols, uv_user_sent);
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

    static hls::stream<rgb_fifo_t> roi_wb_to_gamma("roi_wb_to_gamma");
    static hls::stream<rgb_fifo_t> gamma_to_denoise("gamma_to_denoise");
    static hls::stream<rgb_fifo_t> denoise_to_yuv("denoise_to_yuv");
#pragma HLS STREAM variable=roi_wb_to_gamma depth=128
#pragma HLS STREAM variable=gamma_to_denoise depth=128
#pragma HLS STREAM variable=denoise_to_yuv depth=128

    ap_uint<16> proc_rows = clamp_rows(rows);
    ap_uint<16> proc_cols = clamp_cols(cols);
    RoiWindow roi = pack_roi_window(proc_rows, proc_cols, roi_x, roi_y, roi_w, roi_h);

    stage_roi_wb_processing(src, roi_wb_to_gamma,
                            proc_rows, proc_cols, roi,
                            wb_r_gain, wb_g_gain, wb_b_gain,
                            input_is_bgr);

    stage_gamma_processing(roi_wb_to_gamma, gamma_to_denoise,
                           proc_rows, proc_cols, gamma_mode);

    stage_denoise_processing(gamma_to_denoise, denoise_to_yuv,
                             proc_rows, proc_cols, denoise_strength);

    stage_yuv_conversion(denoise_to_yuv, y_dst, uv_dst,
                         proc_rows, proc_cols);
}
