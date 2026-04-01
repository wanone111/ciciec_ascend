#include <ap_int.h>
#include <opencv2/opencv.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

extern "C" void vision_pre(ap_uint<32>* input_bgr24,
							ap_uint<8>* output_bin8,
							int rows,
							int cols,
							int size,
							ap_uint<8> thresh_val);

int main() {
	auto now = std::chrono::system_clock::now();
	auto time_t_now = std::chrono::system_clock::to_time_t(now);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

	std::stringstream timestamp;
	timestamp << std::put_time(std::localtime(&time_t_now), "%H%M%S") << "_" << std::setfill('0') << std::setw(3)
			  << ms.count();

	std::string input_filename = "/home/bb/competition_ws/ciciec_ascend/image/input/000004812.jpg";
	std::string output_filename =
		"/home/bb/competition_ws/ciciec_ascend/image/output/vision_pre_output_" + timestamp.str() + ".png";

	cv::Mat img_bgr = cv::imread(input_filename, cv::IMREAD_COLOR);
	if (img_bgr.empty()) {
		std::cerr << "Error: Could not load input image: " << input_filename << std::endl;
		return -1;
	}

	int rows = img_bgr.rows;
	int cols = img_bgr.cols;
	int size = rows * cols;
	ap_uint<8> thresh_val = 128;

	std::cout << "Loaded BGR image: " << cols << "x" << rows << " = " << size << " pixels" << std::endl;
	std::cout << "Using threshold value: " << static_cast<int>(thresh_val) << std::endl;
	std::cout << "Output will be saved as: " << output_filename << std::endl;

	ap_uint<32>* input_bgr24 = new ap_uint<32>[size];
	ap_uint<8>* output_bin8 = new ap_uint<8>[size];

	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			cv::Vec3b pixel = img_bgr.at<cv::Vec3b>(i, j);
			ap_uint<32> packed = 0;
			packed.range(7, 0) = pixel[0];
			packed.range(15, 8) = pixel[1];
			packed.range(23, 16) = pixel[2];
			input_bgr24[i * cols + j] = packed;
		}
	}

	vision_pre(input_bgr24, output_bin8, rows, cols, size, thresh_val);

	cv::Mat bin_img(rows, cols, CV_8UC1);
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < cols; j++) {
			bin_img.at<uchar>(i, j) = static_cast<unsigned char>(output_bin8[i * cols + j]);
		}
	}

	cv::Mat gray_ref;
	cv::Mat bin_ref;
	cv::cvtColor(img_bgr, gray_ref, cv::COLOR_BGR2GRAY);
	cv::threshold(gray_ref, bin_ref, static_cast<int>(thresh_val), 255, cv::THRESH_BINARY);

	int white_pixels = 0;
	int black_pixels = 0;
	int mismatch = 0;
	for (int i = 0; i < size; i++) {
		unsigned char hw = static_cast<unsigned char>(output_bin8[i]);
		unsigned char sw = bin_ref.data[i];
		if (hw == 255) {
			white_pixels++;
		} else {
			black_pixels++;
		}
		if (hw != sw) {
			mismatch++;
		}
	}

	std::cout << "First 10 pixels:" << std::endl;
	for (int i = 0; i < 10 && i < size; i++) {
		int r = i / cols;
		int c = i % cols;
		cv::Vec3b pixel = img_bgr.at<cv::Vec3b>(r, c);
		std::cout << "BGR(" << static_cast<int>(pixel[0]) << "," << static_cast<int>(pixel[1]) << ","
				  << static_cast<int>(pixel[2]) << ") -> HW_BIN=" << static_cast<int>(output_bin8[i])
				  << " REF_BIN=" << static_cast<int>(bin_ref.data[i]) << std::endl;
	}

	std::cout << "Vision pre-processing completed!" << std::endl;
	std::cout << "Statistics:" << std::endl;
	std::cout << "  White pixels (255): " << white_pixels << " (" << (white_pixels * 100.0 / size) << "%)"
			  << std::endl;
	std::cout << "  Black pixels (0): " << black_pixels << " (" << (black_pixels * 100.0 / size) << "%)"
			  << std::endl;
	std::cout << "  Mismatch vs OpenCV golden: " << mismatch << " (" << (mismatch * 100.0 / size) << "%)"
			  << std::endl;

	if (mismatch == 0) {
		std::cout << "Test PASSED." << std::endl;
	} else {
		std::cout << "Test FAILED." << std::endl;
	}

	cv::imwrite(output_filename, bin_img);

	delete[] input_bgr24;
	delete[] output_bin8;

	return (mismatch == 0) ? 0 : 1;
}