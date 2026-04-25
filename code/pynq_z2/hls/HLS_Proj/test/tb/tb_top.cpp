#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "top.h"

struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static axis_rgb_t make_axis(uint8_t r, uint8_t g, uint8_t b, bool user, bool last) {
    axis_rgb_t v;
    v.data.range(23, 16) = r;
    v.data.range(15, 8)  = g;
    v.data.range(7, 0)   = b;
    v.keep = 0x7;
    v.strb = 0x7;
    v.user = user ? 1 : 0;
    v.last = last ? 1 : 0;
    v.id   = 0;
    v.dest = 0;
    return v;
}

static RGB unpack_rgb(const axis_rgb_t &v) {
    RGB p;
    p.r = (uint8_t)v.data.range(23, 16);
    p.g = (uint8_t)v.data.range(15, 8);
    p.b = (uint8_t)v.data.range(7, 0);
    return p;
}

static uint8_t gray_ref(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)(((int)r * 77 + (int)g * 150 + (int)b * 29 + 128) >> 8);
}

static void push_frame(hls::stream<axis_rgb_t> &src, const std::vector<RGB> &img, int rows, int cols) {
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const RGB &p = img[r * cols + c];
            src.write(make_axis(p.r, p.g, p.b, (r == 0 && c == 0), (c == cols - 1)));
        }
    }
}

static std::vector<RGB> pop_frame(hls::stream<axis_rgb_t> &dst, int rows, int cols) {
    std::vector<RGB> out(rows * cols);
    for (int i = 0; i < rows * cols; ++i) {
        out[i] = unpack_rgb(dst.read());
    }
    return out;
}

static std::vector<RGB> build_test_image(int rows, int cols) {
    std::vector<RGB> img(rows * cols);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            RGB p;
            p.r = (uint8_t)(r * 20 + c * 3 + 10);
            p.g = (uint8_t)(r * 10 + c * 7 + 20);
            p.b = (uint8_t)(r * 5 + c * 11 + 30);
            img[r * cols + c] = p;
        }
    }
    return img;
}

static void test_bypass_background_rgb() {
    const int rows = 4;
    const int cols = 6;
    std::vector<RGB> img = build_test_image(rows, cols);

    hls::stream<axis_rgb_t> src, dst;
    push_frame(src, img, rows, cols);

    video_preprocess(
        src, dst,
        rows, cols,
        2, 1, 2, 2,
        PROC_RGB,
        128,
        256, 256, 256,
        0,
        0,
        ROI_BG_BYPASS);

    std::vector<RGB> out = pop_frame(dst, rows, cols);

    for (int i = 0; i < rows * cols; ++i) {
        assert(out[i].r == img[i].r);
        assert(out[i].g == img[i].g);
        assert(out[i].b == img[i].b);
    }
    std::cout << "[PASS] test_bypass_background_rgb\n";
}

static void test_zero_background_gray() {
    const int rows = 4;
    const int cols = 6;
    std::vector<RGB> img = build_test_image(rows, cols);

    hls::stream<axis_rgb_t> src, dst;
    push_frame(src, img, rows, cols);

    const int roi_x = 1;
    const int roi_y = 1;
    const int roi_w = 3;
    const int roi_h = 2;

    video_preprocess(
        src, dst,
        rows, cols,
        roi_x, roi_y, roi_w, roi_h,
        PROC_GRAY,
        128,
        256, 256, 256,
        0,
        0,
        ROI_BG_ZERO);

    std::vector<RGB> out = pop_frame(dst, rows, cols);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const RGB &o = out[r * cols + c];
            const bool in_roi = (r >= roi_y && r < roi_y + roi_h && c >= roi_x && c < roi_x + roi_w);
            if (in_roi) {
                uint8_t gray = gray_ref(img[r * cols + c].r, img[r * cols + c].g, img[r * cols + c].b);
                assert(o.r == gray && o.g == gray && o.b == gray);
            } else {
                assert(o.r == 0 && o.g == 0 && o.b == 0);
            }
        }
    }
    std::cout << "[PASS] test_zero_background_gray\n";
}

static void test_threshold_roi() {
    const int rows = 4;
    const int cols = 6;
    std::vector<RGB> img = build_test_image(rows, cols);

    hls::stream<axis_rgb_t> src, dst;
    push_frame(src, img, rows, cols);

    const int roi_x = 0;
    const int roi_y = 0;
    const int roi_w = 2;
    const int roi_h = 2;
    const uint8_t thr = 40;

    video_preprocess(
        src, dst,
        rows, cols,
        roi_x, roi_y, roi_w, roi_h,
        PROC_THRESH,
        thr,
        256, 256, 256,
        0,
        0,
        ROI_BG_BYPASS);

    std::vector<RGB> out = pop_frame(dst, rows, cols);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const RGB &o = out[r * cols + c];
            const bool in_roi = (r >= roi_y && r < roi_y + roi_h && c >= roi_x && c < roi_x + roi_w);
            if (in_roi) {
                uint8_t gray = gray_ref(img[r * cols + c].r, img[r * cols + c].g, img[r * cols + c].b);
                uint8_t binv = (gray >= thr) ? 255 : 0;
                assert(o.r == binv && o.g == binv && o.b == binv);
            } else {
                assert(o.r == img[r * cols + c].r);
                assert(o.g == img[r * cols + c].g);
                assert(o.b == img[r * cols + c].b);
            }
        }
    }
    std::cout << "[PASS] test_threshold_roi\n";
}

int main() {
    test_bypass_background_rgb();
    test_zero_background_gray();
    test_threshold_roi();
    std::cout << "All tests passed.\n";
    return 0;
}
