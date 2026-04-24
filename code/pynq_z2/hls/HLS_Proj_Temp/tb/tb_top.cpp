#include <cstdint>
#include <iostream>

#include "top.h"

static axi_pixel_t make_pixel(uint8_t r, uint8_t g, uint8_t b, bool last) {
	axi_pixel_t p;
	p.data.range(7, 0) = b;
	p.data.range(15, 8) = g;
	p.data.range(23, 16) = r;
	p.keep = -1;
	p.strb = -1;
	p.user = 0;
	p.last = last ? 1 : 0;
	p.id = 0;
	p.dest = 0;
	return p;
}

int main() {
	const ap_uint<16> rows = 8;
	const ap_uint<16> cols = 8;

	hls::stream<axi_pixel_t> src;
	hls::stream<axi_pixel_t> dst;

	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < cols; ++c) {
			uint8_t v = (uint8_t)(r * 16 + c * 8);
			src.write(make_pixel(v, v, v, (c == cols - 1)));
		}
	}

	video_preprocess(
		src,
		dst,
		rows,
		cols,
		2,
		2,
		4,
		4,
		1,
		1,
		96);

	int total = 0;
	int white_pixels = 0;
	int nonzero_outside_roi = 0;

	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < cols; ++c) {
			axi_pixel_t p = dst.read();
			++total;

			uint8_t b = (uint8_t)p.data.range(7, 0);
			uint8_t g = (uint8_t)p.data.range(15, 8);
			uint8_t rr = (uint8_t)p.data.range(23, 16);

			bool in_roi = (r >= 2 && r < 6 && c >= 2 && c < 6);
			bool is_white = (rr == 255 && g == 255 && b == 255);
			bool nonzero = (rr != 0 || g != 0 || b != 0);

			if (is_white) {
				++white_pixels;
			}
			if (!in_roi && nonzero) {
				++nonzero_outside_roi;
			}
		}
	}

	std::cout << "[TB] total=" << total << " white=" << white_pixels
			  << " nonzero_outside_roi=" << nonzero_outside_roi << std::endl;

	if (total != rows * cols) {
		std::cerr << "[TB][FAIL] output pixel count mismatch" << std::endl;
		return 1;
	}
	if (nonzero_outside_roi != 0) {
		std::cerr << "[TB][FAIL] ROI masking failed" << std::endl;
		return 1;
	}

	std::cout << "[TB][PASS] basic skeleton behavior is correct" << std::endl;
	return 0;
}
