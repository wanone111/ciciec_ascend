
#include "rgb2yuv.h"
#include <cstdint>
#include "video_params.h"


static void unpack_rgb(ap_uint<24> pix, ap_uint<1> input_is_bgr, ap_uint<8>& r, ap_uint<8>& g, ap_uint<8>& b) {
    #pragma HLS INLINE
    ap_uint<8> ch0 = pix.range(7, 0);
    ap_uint<8> ch1 = pix.range(15, 8);
    ap_uint<8> ch2 = pix.range(23, 16);
    r = input_is_bgr ? ch0 : ch2;
    g = ch1;
    b = input_is_bgr ? ch2 : ch0;
}


static ap_uint<8> calc_y(ap_uint<8> r, ap_uint<8> g, ap_uint<8> b) {
    #pragma HLS INLINE
    int32_t acc = CSC_Y_R_COEF * (int32_t)r + CSC_Y_G_COEF * (int32_t)g + CSC_Y_B_COEF * (int32_t)b + CSC_Y_OFFSET;
    return saturate((acc + 128) >> 8);
}


static ap_uint<8> calc_u(ap_uint<8> r, ap_uint<8> g, ap_uint<8> b) {
    #pragma HLS INLINE
    int32_t acc = CSC_U_R_COEF * (int32_t)r + CSC_U_G_COEF * (int32_t)g + CSC_U_B_COEF * (int32_t)b + CSC_U_OFFSET;
    return saturate((acc + 128) >> 8);
}

static ap_uint<8> calc_v(ap_uint<8> r, ap_uint<8> g, ap_uint<8> b) {
    #pragma HLS INLINE
    int32_t acc = CSC_V_R_COEF * (int32_t)r + CSC_V_G_COEF * (int32_t)g + CSC_V_B_COEF * (int32_t)b + CSC_V_OFFSET;
    return saturate((acc + 128) >> 8);
}

void rgb2yuv420sp_nv12_stream(
    hls::stream<axi_rgb_t>& in_stream,
    hls::stream<axi_u8_t>& y_out,
    hls::stream<axi_u8_t>& uv_out,
    int rows,
    int cols,
    ap_uint<1> input_is_bgr) {
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS INTERFACE axis port=in_stream
#pragma HLS INTERFACE axis port=y_out
#pragma HLS INTERFACE axis port=uv_out
#pragma HLS INTERFACE s_axilite port=rows bundle=control
#pragma HLS INTERFACE s_axilite port=cols bundle=control
#pragma HLS INTERFACE s_axilite port=input_is_bgr bundle=control

    ap_uint<8> prev_row_r[MAX_WIDTH];
    ap_uint<8> prev_row_g[MAX_WIDTH];
    ap_uint<8> prev_row_b[MAX_WIDTH];
#pragma HLS BIND_STORAGE variable=prev_row_r type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=prev_row_g type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=prev_row_b type=ram_2p impl=bram

    int proc_rows = (rows > MAX_HEIGHT) ? MAX_HEIGHT : rows;
    int proc_cols = (cols > MAX_WIDTH) ? MAX_WIDTH : cols;
    int even_cols = (proc_cols >> 1) << 1;

    bool uv_user_sent = false;
    ap_uint<8> left_r = 0, left_g = 0, left_b = 0;
    ap_uint<8> last_uv_r_val = 0, last_uv_g_val = 0, last_uv_b_val = 0;

row_loop:
    for (int r = 0; r < proc_rows; ++r) {
        // Frame boundary: reset UV user flag when new frame starts
        if (r == 0) {
            uv_user_sent = false;
        }

        ap_uint<8> top_left_r = 0, top_left_g = 0, top_left_b = 0;
    col_loop:
        for (int c = 0; c < proc_cols; ++c) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=1280
            ap_uint<8> top_r = (r == 0) ? ap_uint<8>(0) : prev_row_r[c];
            ap_uint<8> top_g = (r == 0) ? ap_uint<8>(0) : prev_row_g[c];
            ap_uint<8> top_b = (r == 0) ? ap_uint<8>(0) : prev_row_b[c];

            axi_rgb_t in_pix = in_stream.read();
            ap_uint<8> rv, gv, bv;
            unpack_rgb(in_pix.data, input_is_bgr, rv, gv, bv);

            // Output Y luma
            axi_u8_t y_pkt;
            y_pkt.data = calc_y(rv, gv, bv);
            y_pkt.keep = 1;
            y_pkt.strb = 1;
            y_pkt.user = (r == 0 && c == 0) ? 1 : 0;
            y_pkt.last = (c == proc_cols - 1) ? 1 : 0;
            y_pkt.id = in_pix.id;
            y_pkt.dest = in_pix.dest;
            y_out.write(y_pkt);

            // Handle 2x2 chroma averaging: emit UV when we have complete 2x2 block
            if (r > 0 && (r & 1) == 1 && c > 0 && (c & 1) == 1) {
                ap_uint<8> tl_r = top_left_r;
                ap_uint<8> tl_g = top_left_g;
                ap_uint<8> tl_b = top_left_b;

                ap_uint<8> tr_r = top_r;
                ap_uint<8> tr_g = top_g;
                ap_uint<8> tr_b = top_b;

                ap_uint<8> bl_r = left_r;
                ap_uint<8> bl_g = left_g;
                ap_uint<8> bl_b = left_b;

                uint16_t avg_r = ((uint16_t)tl_r + (uint16_t)tr_r + (uint16_t)bl_r + (uint16_t)rv + 2) >> 2;
                uint16_t avg_g = ((uint16_t)tl_g + (uint16_t)tr_g + (uint16_t)bl_g + (uint16_t)gv + 2) >> 2;
                uint16_t avg_b = ((uint16_t)tl_b + (uint16_t)tr_b + (uint16_t)bl_b + (uint16_t)bv + 2) >> 2;

                // Cache last valid UV for edge reuse
                last_uv_r_val = (ap_uint<8>)avg_r;
                last_uv_g_val = (ap_uint<8>)avg_g;
                last_uv_b_val = (ap_uint<8>)avg_b;

                axi_u8_t u_pkt, v_pkt;
                u_pkt.data = calc_u((ap_uint<8>)avg_r, (ap_uint<8>)avg_g, (ap_uint<8>)avg_b);
                u_pkt.keep = 1;
                u_pkt.strb = 1;
                u_pkt.user = uv_user_sent ? 0 : 1;
                u_pkt.last = 0;
                u_pkt.id = in_pix.id;
                u_pkt.dest = in_pix.dest;
                uv_out.write(u_pkt);
                uv_user_sent = true;

                // AXI4-Stream Video convention: TLAST marks end of each UV row.
                bool is_uv_line_end = (c == even_cols - 1);
                v_pkt.data = calc_v((ap_uint<8>)avg_r, (ap_uint<8>)avg_g, (ap_uint<8>)avg_b);
                v_pkt.keep = 1;
                v_pkt.strb = 1;
                v_pkt.user = 0;
                v_pkt.last = is_uv_line_end ? 1 : 0;
                v_pkt.id = in_pix.id;
                v_pkt.dest = in_pix.dest;
                uv_out.write(v_pkt);
            }

            // Cache left pixel for next iteration (odd columns)
            if ((c & 1) == 0) {
                left_r = rv;
                left_g = gv;
                left_b = bv;
            }

            // Save to previous row for next row processing
            prev_row_r[c] = rv;
            prev_row_g[c] = gv;
            prev_row_b[c] = bv;

            top_left_r = top_r;
            top_left_g = top_g;
            top_left_b = top_b;
        }

        // Standard NV12 conservative approach: 
        // Only complete 2x2 blocks produce UV pairs. Edge pixels (odd rows/cols) are discarded.
        // If upgrades to edge-fill are needed, cached last_uv_r_val/g_val/b_val are available.
    }
}