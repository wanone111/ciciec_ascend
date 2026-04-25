#include "top.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>

// ============================================================================
// Enhanced Testbench for Unified Video Preprocessing IP
// Tests: ISP + Dynamic ROI + RGB2YUV conversion
// Covers: Normal cases, edge cases, odd dimensions, various ISP modes
// ============================================================================

struct TestResult {
    bool passed;
    int y_count;
    int uv_count;
    int expected_y;
    int expected_uv;
    std::string message;
};

// Helper function to run a single test case
TestResult run_test(const char* test_name,
                    int rows, int cols,
                    int roi_x, int roi_y, int roi_w, int roi_h,
                    int wb_r_gain, int wb_g_gain, int wb_b_gain,
                    int gamma_mode, int denoise_strength,
                    int input_is_bgr) {

    TestResult result;
    result.passed = true;
    result.message = "";

    std::cout << "\n=== Test: " << test_name << " ===" << std::endl;
    std::cout << "Resolution: " << cols << "x" << rows << std::endl;
    std::cout << "ROI: (" << roi_x << "," << roi_y << ") " << roi_w << "x" << roi_h << std::endl;

    // Create input/output streams
    hls::stream<axi_rgb_t> src_stream("src_stream");
    hls::stream<axi_u8_t> y_dst_stream("y_dst_stream");
    hls::stream<axi_u8_t> uv_dst_stream("uv_dst_stream");

    // Generate test input pattern (gradient)
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            axi_rgb_t pix;
            // Create gradient pattern: R increases horizontally, B increases vertically
            pix.data.range(23, 16) = (c * 255 / cols) & 0xFF;  // R
            pix.data.range(15, 8) = 128;                        // G
            pix.data.range(7, 0) = (r * 255 / rows) & 0xFF;    // B
            pix.keep = 1;
            pix.strb = 1;
            pix.user = (r == 0 && c == 0) ? 1 : 0;
            pix.last = (c == cols - 1) ? 1 : 0;
            pix.id = 0;
            pix.dest = 0;
            src_stream.write(pix);
        }
    }

    // Call the DUT
    video_preprocess(
        src_stream,
        y_dst_stream,
        uv_dst_stream,
        rows,
        cols,
        roi_x,
        roi_y,
        roi_w,
        roi_h,
        wb_r_gain,
        wb_g_gain,
        wb_b_gain,
        gamma_mode,
        denoise_strength,
        input_is_bgr
    );

    // Verify Y output
    int y_count = 0;
    bool first_y = true;
    bool y_user_ok = false;

    while (!y_dst_stream.empty()) {
        axi_u8_t y_pkt = y_dst_stream.read();

        if (first_y) {
            y_user_ok = (y_pkt.user == 1);
            if (!y_user_ok) {
                result.message += "ERROR: First Y packet should have user=1. ";
                result.passed = false;
            }
            first_y = false;
        }

        y_count++;
    }

    result.y_count = y_count;
    result.expected_y = rows * cols;

    if (y_count != result.expected_y) {
        result.message += "ERROR: Y count mismatch (got " + std::to_string(y_count) +
                         ", expected " + std::to_string(result.expected_y) + "). ";
        result.passed = false;
    }

    // Verify UV output
    int uv_count = 0;
    bool first_uv = true;
    bool uv_user_ok = false;

    while (!uv_dst_stream.empty()) {
        axi_u8_t uv_pkt = uv_dst_stream.read();

        if (first_uv) {
            uv_user_ok = (uv_pkt.user == 1);
            if (!uv_user_ok) {
                result.message += "ERROR: First UV packet should have user=1. ";
                result.passed = false;
            }
            first_uv = false;
        }

        uv_count++;
    }

    result.uv_count = uv_count;
    // NV12: U and V for each 2x2 block, so (rows/2) * cols pixels (interleaved)
    int even_rows = (rows >> 1) << 1;
    int even_cols = (cols >> 1) << 1;
    result.expected_uv = (even_rows / 2) * even_cols;

    if (uv_count != result.expected_uv) {
        result.message += "ERROR: UV count mismatch (got " + std::to_string(uv_count) +
                         ", expected " + std::to_string(result.expected_uv) + "). ";
        result.passed = false;
    }

    if (result.passed) {
        result.message = "PASSED";
        std::cout << "✓ PASSED" << std::endl;
    } else {
        std::cout << "✗ FAILED: " << result.message << std::endl;
    }

    std::cout << "  Y packets: " << y_count << "/" << result.expected_y << std::endl;
    std::cout << "  UV packets: " << uv_count << "/" << result.expected_uv << std::endl;

    return result;
}

int main() {
    std::cout << "=== Video Preprocess HLS Enhanced Testbench ===" << std::endl;
    std::cout << "Testing: ISP (WB+Gamma+Denoise) -> Dynamic ROI -> RGB2YUV" << std::endl;

    int total_tests = 0;
    int passed_tests = 0;

    // =========================================================================
    // Test 1: Basic functionality with bypass mode (original test)
    // =========================================================================
    total_tests++;
    TestResult t1 = run_test(
        "Basic 640x480 with ROI center, ISP bypass",
        480, 640,           // rows, cols
        160, 120, 320, 240, // ROI: x, y, w, h
        256, 256, 256,      // WB gains (1.0x)
        GAMMA_MODE_BYPASS,  // Gamma bypass
        0,                  // No denoise
        0                   // RGB input
    );
    if (t1.passed) passed_tests++;

    // =========================================================================
    // Test 2: Odd dimensions (edge case for chroma subsampling)
    // =========================================================================
    total_tests++;
    TestResult t2 = run_test(
        "Odd dimensions 641x481",
        481, 641,           // rows, cols (both odd)
        0, 0, 641, 481,     // Full frame ROI
        256, 256, 256,      // WB gains (1.0x)
        GAMMA_MODE_BYPASS,  // Gamma bypass
        0,                  // No denoise
        0                   // RGB input
    );
    if (t2.passed) passed_tests++;

    // =========================================================================
    // Test 3: Small resolution
    // =========================================================================
    total_tests++;
    TestResult t3 = run_test(
        "Small 8x8 resolution",
        8, 8,               // rows, cols
        0, 0, 8, 8,         // Full frame ROI
        256, 256, 256,      // WB gains (1.0x)
        GAMMA_MODE_BYPASS,  // Gamma bypass
        0,                  // No denoise
        0                   // RGB input
    );
    if (t3.passed) passed_tests++;

    // =========================================================================
    // Test 4: Bright gamma mode
    // =========================================================================
    total_tests++;
    TestResult t4 = run_test(
        "Bright gamma mode",
        240, 320,           // rows, cols
        0, 0, 320, 240,     // Full frame ROI
        256, 256, 256,      // WB gains (1.0x)
        GAMMA_MODE_BRIGHT,  // Bright gamma
        0,                  // No denoise
        0                   // RGB input
    );
    if (t4.passed) passed_tests++;

    // =========================================================================
    // Test 5: Dark gamma mode
    // =========================================================================
    total_tests++;
    TestResult t5 = run_test(
        "Dark gamma mode",
        240, 320,           // rows, cols
        0, 0, 320, 240,     // Full frame ROI
        256, 256, 256,      // WB gains (1.0x)
        GAMMA_MODE_DARK,    // Dark gamma
        0,                  // No denoise
        0                   // RGB input
    );
    if (t5.passed) passed_tests++;

    // =========================================================================
    // Test 6: White balance with gain
    // =========================================================================
    total_tests++;
    TestResult t6 = run_test(
        "White balance boost (R=1.5x, B=1.2x)",
        240, 320,           // rows, cols
        0, 0, 320, 240,     // Full frame ROI
        384, 256, 307,      // WB gains (R=1.5x, G=1.0x, B=1.2x)
        GAMMA_MODE_BYPASS,  // Gamma bypass
        0,                  // No denoise
        0                   // RGB input
    );
    if (t6.passed) passed_tests++;

    // =========================================================================
    // Test 7: Denoising enabled
    // =========================================================================
    total_tests++;
    TestResult t7 = run_test(
        "Denoising enabled (strength=128)",
        240, 320,           // rows, cols
        0, 0, 320, 240,     // Full frame ROI
        256, 256, 256,      // WB gains (1.0x)
        GAMMA_MODE_BYPASS,  // Gamma bypass
        128,                // Medium denoise
        0                   // RGB input
    );
    if (t7.passed) passed_tests++;

    // =========================================================================
    // Test 8: BGR input format
    // =========================================================================
    total_tests++;
    TestResult t8 = run_test(
        "BGR input format",
        240, 320,           // rows, cols
        0, 0, 320, 240,     // Full frame ROI
        256, 256, 256,      // WB gains (1.0x)
        GAMMA_MODE_BYPASS,  // Gamma bypass
        0,                  // No denoise
        1                   // BGR input
    );
    if (t8.passed) passed_tests++;

    // =========================================================================
    // Test 9: Partial ROI (not full frame)
    // =========================================================================
    total_tests++;
    TestResult t9 = run_test(
        "Partial ROI (quarter frame)",
        480, 640,           // rows, cols
        0, 0, 320, 240,     // Top-left quarter ROI
        256, 256, 256,      // WB gains (1.0x)
        GAMMA_MODE_BYPASS,  // Gamma bypass
        0,                  // No denoise
        0                   // RGB input
    );
    if (t9.passed) passed_tests++;

    // =========================================================================
    // Test 10: Combined ISP effects
    // =========================================================================
    total_tests++;
    TestResult t10 = run_test(
        "Combined: WB + Gamma + Denoise",
        360, 640,           // rows, cols
        100, 80, 440, 200,  // Center ROI
        307, 256, 358,      // WB gains (R=1.2x, G=1.0x, B=1.4x)
        GAMMA_MODE_DARK,    // Dark gamma
        64,                 // Light denoise
        0                   // RGB input
    );
    if (t10.passed) passed_tests++;

    // =========================================================================
    // Summary
    // =========================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Passed: " << passed_tests << "/" << total_tests << std::endl;

    if (passed_tests == total_tests) {
        std::cout << "\n✓ ALL TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗ SOME TESTS FAILED!" << std::endl;
        std::cout << "Failed: " << (total_tests - passed_tests) << std::endl;
        return 1;
    }
}