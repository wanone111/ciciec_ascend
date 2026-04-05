#include <cstdint>
#include <cstdio>
#include <vector>

#include "top.h"

static inline uint8_t sw_gray_from_bgr(uint8_t b, uint8_t g, uint8_t r) {
	// 近似 ITU-R BT.601：Y ~= 0.114B + 0.587G + 0.299R
	return (uint8_t)(((uint16_t)r * 77 + (uint16_t)g * 150 + (uint16_t)b * 29) >> 8);
}

int main() {
	const int rows = 64;
	const int cols = 96;
	const int size = rows * cols;
	const ap_uint<8> thresh = kDefaultThreshold;
	const int roi_x = 7;
	const int roi_y = 5;
	const int roi_w = 41;
	const int roi_h = 27;
	const int aligned_x = roi_x & ~1;
	const int aligned_y = roi_y & ~1;
	const int aligned_w = roi_w & ~1;
	const int aligned_h = roi_h & ~1;

	const int roi_size = aligned_w * aligned_h;
	std::vector<ap_uint<INPUT_PTR_WIDTH>> input(size);
	std::vector<ap_uint<OUTPUT_PTR_WIDTH>> output_hw(roi_size);
	std::vector<uint8_t> output_sw(roi_size, 0);

	for (int i = 0; i < size; ++i) {
		int row = i / cols;
		int col = i % cols;
		uint8_t b = 0;
		uint8_t g = 0;
		uint8_t r = 0;
		if (row >= aligned_y && row < (aligned_y + aligned_h) && col >= aligned_x && col < (aligned_x + aligned_w)) {
			b = 240;
			g = 240;
			r = 240;
		}

		ap_uint<INPUT_PTR_WIDTH> pix = 0;
		pix.range(7, 0) = b;
		pix.range(15, 8) = g;
		pix.range(23, 16) = r;
		input[i] = pix;

		if (row >= aligned_y && row < (aligned_y + aligned_h) && col >= aligned_x && col < (aligned_x + aligned_w)) {
			uint8_t gray = sw_gray_from_bgr(b, g, r);
			int roi_row = row - aligned_y;
			int roi_col = col - aligned_x;
			output_sw[roi_row * aligned_w + roi_col] = (gray > (uint8_t)thresh) ? 255 : 0;
		}
	}

	vision_preprocess_accel(input.data(), output_hw.data(), rows, cols, roi_x, roi_y, roi_w, roi_h, thresh);

	int mismatch = 0;
	for (int i = 0; i < roi_size; ++i) {
		uint8_t hw = (uint8_t)output_hw[i];
		if (hw != output_sw[i]) {
			if (mismatch < 10) {
				std::printf("Mismatch at %d: hw=%u sw=%u\n", i, hw, output_sw[i]);
			}
			++mismatch;
		}
	}

	if (mismatch == 0) {
		std::printf("TEST PASSED: rows=%d cols=%d roi=(%d,%d,%d,%d) threshold=%u\n",
		            rows, cols, aligned_x, aligned_y, aligned_w, aligned_h, (unsigned)thresh);
		return 0;
	}

	std::printf("TEST FAILED: mismatch=%d\n", mismatch);
	return 1;
}
