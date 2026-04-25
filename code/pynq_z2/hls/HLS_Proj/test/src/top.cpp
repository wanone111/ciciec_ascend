#include "top.h"
#include "roi.h"
#include "isp.h"

struct PixelPkt {
    ap_uint<8> r;
    ap_uint<8> g;
    ap_uint<8> b;
    bool in_roi;
    bool row_first;
    ap_uint<1> user;
    ap_uint<1> last;
    ap_uint<3> keep;
    ap_uint<3> strb;
    ap_uint<1> id;
    ap_uint<1> dest;
};

namespace {

static ap_uint<16> clamp_rows(ap_uint<16> rows) {
#pragma HLS INLINE
    if (rows == 0) return 1;
    if (rows > MAX_HEIGHT) return MAX_HEIGHT;
    return rows;
}

static ap_uint<16> clamp_cols(ap_uint<16> cols) {
#pragma HLS INLINE
    if (cols == 0) return 1;
    if (cols > MAX_WIDTH) return MAX_WIDTH;
    return cols;
}

void stage_read_and_tag(
    hls::stream<axis_rgb_t> &src,
    hls::stream<PixelPkt> &s0,
    ap_uint<16> proc_rows,
    ap_uint<16> proc_cols,
    const RoiWindow &roi) {
#pragma HLS INLINE off
row_loop_read:
    for (ap_uint<16> r = 0; r < proc_rows; ++r) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_HEIGHT avg=360
        bool row_in_roi = roi.valid && (r >= roi.y0) && (r <= roi.y1);
    col_loop_read:
        for (ap_uint<16> c = 0; c < proc_cols; ++c) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_WIDTH avg=640
            axis_rgb_t in = src.read();
            PixelPkt pkt;
            pkt.r = in.data.range(23, 16);
            pkt.g = in.data.range(15, 8);
            pkt.b = in.data.range(7, 0);
            pkt.in_roi = row_in_roi && roi.valid && (c >= roi.x0) && (c <= roi.x1);
            pkt.row_first = (c == 0);
            pkt.user = in.user;
            pkt.last = in.last;
            pkt.keep = in.keep;
            pkt.strb = in.strb;
            pkt.id   = in.id;
            pkt.dest = in.dest;
            s0.write(pkt);
        }
    }
}

void stage_wb_gamma_lut(
    hls::stream<PixelPkt> &s0,
    hls::stream<PixelPkt> &s1,
    ap_uint<16> proc_rows,
    ap_uint<16> proc_cols,
    const ap_uint<8> lut_r[256],
    const ap_uint<8> lut_g[256],
    const ap_uint<8> lut_b[256]) {
#pragma HLS INLINE off
row_loop_lut:
    for (ap_uint<16> r = 0; r < proc_rows; ++r) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_HEIGHT avg=360
    col_loop_lut:
        for (ap_uint<16> c = 0; c < proc_cols; ++c) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_WIDTH avg=640
            PixelPkt pkt = s0.read();
            if (pkt.in_roi) {
                pkt.r = lut_r[(unsigned)pkt.r];
                pkt.g = lut_g[(unsigned)pkt.g];
                pkt.b = lut_b[(unsigned)pkt.b];
            }
            s1.write(pkt);
        }
    }
}

void stage_denoise(
    hls::stream<PixelPkt> &s1,
    hls::stream<PixelPkt> &s2,
    ap_uint<16> proc_rows,
    ap_uint<16> proc_cols,
    bool denoise_enable) {
#pragma HLS INLINE off
    ap_uint<8> prev_r = 0;
    ap_uint<8> prev_g = 0;
    ap_uint<8> prev_b = 0;

row_loop_dn:
    for (ap_uint<16> r = 0; r < proc_rows; ++r) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_HEIGHT avg=360
    col_loop_dn:
        for (ap_uint<16> c = 0; c < proc_cols; ++c) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_WIDTH avg=640
            PixelPkt pkt = s1.read();
            if (pkt.row_first) {
                prev_r = pkt.r;
                prev_g = pkt.g;
                prev_b = pkt.b;
            }

            if (pkt.in_roi) {
                ap_uint<8> out_r = denoise2tap(pkt.r, prev_r, denoise_enable);
                ap_uint<8> out_g = denoise2tap(pkt.g, prev_g, denoise_enable);
                ap_uint<8> out_b = denoise2tap(pkt.b, prev_b, denoise_enable);
                prev_r = pkt.r;
                prev_g = pkt.g;
                prev_b = pkt.b;
                pkt.r = out_r;
                pkt.g = out_g;
                pkt.b = out_b;
            }
            s2.write(pkt);
        }
    }
}

void stage_pack_out(
    hls::stream<PixelPkt> &s2,
    hls::stream<axis_rgb_t> &dst,
    ap_uint<16> proc_rows,
    ap_uint<16> proc_cols,
    ap_uint<2> proc_mode,
    ap_uint<8> threshold_val,
    ap_uint<2> roi_bg_mode) {
#pragma HLS INLINE off
row_loop_out:
    for (ap_uint<16> r = 0; r < proc_rows; ++r) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_HEIGHT avg=360
    col_loop_out:
        for (ap_uint<16> c = 0; c < proc_cols; ++c) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_WIDTH avg=640
            PixelPkt pkt = s2.read();

            ap_uint<8> out_r = pkt.r;
            ap_uint<8> out_g = pkt.g;
            ap_uint<8> out_b = pkt.b;

            if (pkt.in_roi) {
                if (proc_mode == PROC_GRAY) {
                    ap_uint<8> gray = gray_from_rgb(pkt.r, pkt.g, pkt.b);
                    out_r = gray;
                    out_g = gray;
                    out_b = gray;
                } else if (proc_mode == PROC_THRESH) {
                    ap_uint<8> gray = gray_from_rgb(pkt.r, pkt.g, pkt.b);
                    ap_uint<8> binv = (gray >= threshold_val) ? (ap_uint<8>)255 : (ap_uint<8>)0;
                    out_r = binv;
                    out_g = binv;
                    out_b = binv;
                }
            } else {
                if (roi_bg_mode == ROI_BG_ZERO || roi_bg_mode == ROI_BG_CROP_RESERVED) {
                    out_r = 0;
                    out_g = 0;
                    out_b = 0;
                }
            }

            axis_rgb_t out;
            out.data.range(23, 16) = out_r;
            out.data.range(15, 8)  = out_g;
            out.data.range(7, 0)   = out_b;
            out.keep = pkt.keep;
            out.strb = pkt.strb;
            out.user = pkt.user;
            out.last = pkt.last;
            out.id   = pkt.id;
            out.dest = pkt.dest;
            dst.write(out);
        }
    }
}

} // namespace

void video_preprocess(
    hls::stream<axis_rgb_t> &src,
    hls::stream<axis_rgb_t> &dst,
    ap_uint<16> rows,
    ap_uint<16> cols,
    ap_uint<16> roi_x,
    ap_uint<16> roi_y,
    ap_uint<16> roi_w,
    ap_uint<16> roi_h,
    ap_uint<2>  proc_mode,
    ap_uint<8>  threshold_val,
    ap_uint<16> wb_r_gain,
    ap_uint<16> wb_g_gain,
    ap_uint<16> wb_b_gain,
    ap_uint<2>  gamma_mode,
    ap_uint<8>  denoise_strength,
    ap_uint<2>  roi_bg_mode) {
#pragma HLS INTERFACE axis register both port=src
#pragma HLS INTERFACE axis register both port=dst

#pragma HLS INTERFACE s_axilite port=rows bundle=CTRL
#pragma HLS INTERFACE s_axilite port=cols bundle=CTRL
#pragma HLS INTERFACE s_axilite port=roi_x bundle=CTRL
#pragma HLS INTERFACE s_axilite port=roi_y bundle=CTRL
#pragma HLS INTERFACE s_axilite port=roi_w bundle=CTRL
#pragma HLS INTERFACE s_axilite port=roi_h bundle=CTRL
#pragma HLS INTERFACE s_axilite port=proc_mode bundle=CTRL
#pragma HLS INTERFACE s_axilite port=threshold_val bundle=CTRL
#pragma HLS INTERFACE s_axilite port=wb_r_gain bundle=CTRL
#pragma HLS INTERFACE s_axilite port=wb_g_gain bundle=CTRL
#pragma HLS INTERFACE s_axilite port=wb_b_gain bundle=CTRL
#pragma HLS INTERFACE s_axilite port=gamma_mode bundle=CTRL
#pragma HLS INTERFACE s_axilite port=denoise_strength bundle=CTRL
#pragma HLS INTERFACE s_axilite port=roi_bg_mode bundle=CTRL
#pragma HLS INTERFACE s_axilite port=return bundle=CTRL

    const ap_uint<16> proc_rows = clamp_rows(rows);
    const ap_uint<16> proc_cols = clamp_cols(cols);
    const RoiWindow roi = pack_roi_window(proc_rows, proc_cols, roi_x, roi_y, roi_w, roi_h);
    const bool denoise_enable = (denoise_strength != 0);

    ap_uint<8> lut_r[256];
    ap_uint<8> lut_g[256];
    ap_uint<8> lut_b[256];
#pragma HLS BIND_STORAGE variable=lut_r type=ram_1p impl=lutram
#pragma HLS BIND_STORAGE variable=lut_g type=ram_1p impl=lutram
#pragma HLS BIND_STORAGE variable=lut_b type=ram_1p impl=lutram

    build_wb_gamma_lut(wb_r_gain, wb_g_gain, wb_b_gain, gamma_mode, lut_r, lut_g, lut_b);

    hls::stream<PixelPkt> s0("s0");
    hls::stream<PixelPkt> s1("s1");
    hls::stream<PixelPkt> s2("s2");
#pragma HLS STREAM variable=s0 depth=STREAM_DEPTH_PIX
#pragma HLS STREAM variable=s1 depth=STREAM_DEPTH_PIX
#pragma HLS STREAM variable=s2 depth=STREAM_DEPTH_PIX

#pragma HLS DATAFLOW
    stage_read_and_tag(src, s0, proc_rows, proc_cols, roi);
    stage_wb_gamma_lut(s0, s1, proc_rows, proc_cols, lut_r, lut_g, lut_b);
    stage_denoise(s1, s2, proc_rows, proc_cols, denoise_enable);
    stage_pack_out(s2, dst, proc_rows, proc_cols, proc_mode, threshold_val, roi_bg_mode);
}
