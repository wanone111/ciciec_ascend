#include <cstdint>
#include <iostream>
#include <vector>
#include <cmath>

#include "top.h"

// Test case structure for better organization
struct TestCase {
    const char* name;
    int rows;
    int cols;
    bool input_is_bgr;
    bool expect_pass;
};

// Test configuration
static const int MAX_TEST_ERRORS = 0;  // Allow some tolerance if needed

static axi_rgb_t make_pixel(uint8_t r, uint8_t g, uint8_t b, bool input_is_bgr, bool last) {
        axi_rgb_t p;
        if (input_is_bgr) {
                p.data.range(7, 0) = r;
                p.data.range(15, 8) = g;
                p.data.range(23, 16) = b;
        } else {
                p.data.range(7, 0) = b;
                p.data.range(15, 8) = g;
                p.data.range(23, 16) = r;
        }
        p.keep = -1;
        p.strb = -1;
        p.user = 0;
        p.last = last ? 1 : 0;
        p.id = 0;
        p.dest = 0;
        return p;
}

#pragma HLS INLINE
static uint8_t sw_saturate(int32_t val) {
        return (val < 0) ? 0 : (val > 255) ? 255 : static_cast<uint8_t>(val);
}

#pragma HLS INLINE
static uint8_t sw_y(uint8_t r, uint8_t g, uint8_t b) {
        int32_t acc = CSC_Y_R_COEF * (int32_t)r + CSC_Y_G_COEF * (int32_t)g + CSC_Y_B_COEF * (int32_t)b + CSC_Y_OFFSET;
        return sw_saturate((acc + 128) >> 8);
}

#pragma HLS INLINE
static uint8_t sw_u(uint8_t r, uint8_t g, uint8_t b) {
        int32_t acc = CSC_U_R_COEF * (int32_t)r + CSC_U_G_COEF * (int32_t)g + CSC_U_B_COEF * (int32_t)b + CSC_U_OFFSET;
        return sw_saturate((acc + 128) >> 8);
}

#pragma HLS INLINE
static uint8_t sw_v(uint8_t r, uint8_t g, uint8_t b) {
        int32_t acc = CSC_V_R_COEF * (int32_t)r + CSC_V_G_COEF * (int32_t)g + CSC_V_B_COEF * (int32_t)b + CSC_V_OFFSET;
        return sw_saturate((acc + 128) >> 8);
}

static int run_case(int rows, int cols, bool input_is_bgr, const char* test_name) {
        const int uv_cols = (cols >> 1) << 1;

        hls::stream<axi_rgb_t> src;
        hls::stream<axi_u8_t> y_dst;
        hls::stream<axi_u8_t> uv_dst;

        std::vector<uint8_t> in_r(rows * cols);
        std::vector<uint8_t> in_g(rows * cols);
        std::vector<uint8_t> in_b(rows * cols);

        // Generate input pattern
        for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                        int idx = r * cols + c;
                        uint8_t rv = (uint8_t)((r * 37 + c * 13 + 11) & 0xFF);
                        uint8_t gv = (uint8_t)((r * 19 + c * 29 + 7) & 0xFF);
                        uint8_t bv = (uint8_t)((r * 11 + c * 17 + 23) & 0xFF);
                        in_r[idx] = rv;
                        in_g[idx] = gv;
                        in_b[idx] = bv;
                        src.write(make_pixel(rv, gv, bv, input_is_bgr, c == cols - 1));
                }
        }

        video_preprocess(src, y_dst, uv_dst, (ap_uint<16>)rows, (ap_uint<16>)cols, (ap_uint<8>)(input_is_bgr ? 1 : 0));

        int errors = 0;

        // Verify Y channel
        for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                        int idx = r * cols + c;
                        axi_u8_t p = y_dst.read();
                        uint8_t exp_y = sw_y(in_r[idx], in_g[idx], in_b[idx]);
                        if ((uint8_t)p.data != exp_y) {
                                ++errors;
                        }
                        if (r == 0 && c == 0 && p.user != 1) {
                                ++errors;
                        }
                        if (!(r == 0 && c == 0) && p.user != 0) {
                                ++errors;
                        }
                        if ((c == cols - 1 && p.last != 1) || (c != cols - 1 && p.last != 0)) {
                                ++errors;
                        }
                }
        }

        // Verify UV channel
        bool uv_user_seen = false;
        for (int r = 1; r < rows; r += 2) {
                for (int c = 1; c < cols; c += 2) {
                        if (c >= uv_cols) {
                                continue;
                        }
                        int idx_tl = (r - 1) * cols + (c - 1);
                        int idx_tr = (r - 1) * cols + c;
                        int idx_bl = r * cols + (c - 1);
                        int idx_br = r * cols + c;

                        uint8_t avg_r = (uint8_t)((in_r[idx_tl] + in_r[idx_tr] + in_r[idx_bl] + in_r[idx_br] + 2) >> 2);
                        uint8_t avg_g = (uint8_t)((in_g[idx_tl] + in_g[idx_tr] + in_g[idx_bl] + in_g[idx_br] + 2) >> 2);
                        uint8_t avg_b = (uint8_t)((in_b[idx_tl] + in_b[idx_tr] + in_b[idx_bl] + in_b[idx_br] + 2) >> 2);

                        uint8_t exp_u = sw_u(avg_r, avg_g, avg_b);
                        uint8_t exp_v = sw_v(avg_r, avg_g, avg_b);

                        axi_u8_t u_p = uv_dst.read();
                        axi_u8_t v_p = uv_dst.read();

                        if ((uint8_t)u_p.data != exp_u || (uint8_t)v_p.data != exp_v) {
                                ++errors;
                        }

                        if (!uv_user_seen) {
                                if (u_p.user != 1 || v_p.user != 0) {
                                        ++errors;
                                }
                                uv_user_seen = true;
                        } else if (u_p.user != 0 || v_p.user != 0) {
                                ++errors;
                        }

                        int uv_byte_pos = (c - 1) + 1;
                        bool v_last_expected = (uv_byte_pos == uv_cols - 1);
                        if (u_p.last != 0 || (v_last_expected && v_p.last != 1) || (!v_last_expected && v_p.last != 0)) {
                                ++errors;
                        }
                }
        }

        if (!y_dst.empty() || !uv_dst.empty()) {
                ++errors;
        }

        std::cout << "[TB] " << test_name << " mode=" << (input_is_bgr ? "BGR" : "RGB")
                  << " (" << rows << "x" << cols << ") errors=" << errors << std::endl;
        return errors;
}

int main() {
        int total_errors = 0;
        int test_count = 0;
        int pass_count = 0;

        std::cout << "\n========================================" << std::endl;
        std::cout << "CSC Test Suite - Extended Coverage" << std::endl;
        std::cout << "========================================\n" << std::endl;

        // Test case 1: Basic 6x8 RGB
        total_errors += run_case(6, 8, false, "Basic_6x8");
        test_count++;

        // Test case 2: Basic 6x8 BGR
        total_errors += run_case(6, 8, true, "Basic_6x8");
        test_count++;

        // Test case 3: Odd resolution 7x9 (edge cases)
        total_errors += run_case(7, 9, false, "Odd_7x9");
        test_count++;

        // Test case 4: Odd resolution 7x9 BGR
        total_errors += run_case(7, 9, true, "Odd_7x9");
        test_count++;

        // Test case 5: Minimum 2x2
        total_errors += run_case(2, 2, false, "Min_2x2");
        test_count++;

        // Test case 6: Minimum 2x2 BGR
        total_errors += run_case(2, 2, true, "Min_2x2");
        test_count++;

        // Test case 7: VGA resolution 640x480
        total_errors += run_case(480, 640, false, "VGA_640x480");
        test_count++;

        // Test case 8: 720p resolution 1280x720
        total_errors += run_case(720, 1280, false, "HD_1280x720");
        test_count++;

        // Test case 9: Asymmetric odd 5x7
        total_errors += run_case(5, 7, true, "Asym_5x7");
        test_count++;

        // Test case 10: Single row edge (should handle gracefully)
        total_errors += run_case(1, 8, false, "Edge_1x8");
        test_count++;

        std::cout << "\n========================================" << std::endl;
        std::cout << "Test Summary" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Total tests: " << test_count << std::endl;
        std::cout << "Total errors: " << total_errors << std::endl;

        if (total_errors == 0) {
                pass_count = test_count;
                std::cout << "[TB][PASS] All " << pass_count << " tests passed!" << std::endl;
                return 0;
        } else {
                std::cerr << "[TB][FAIL] Functional mismatch detected, total_errors=" << total_errors << std::endl;
                return 1;
        }
}