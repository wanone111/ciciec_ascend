
#include "pose_protocol.h"
#include "ip_video_player.h"

#include <acl/acl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>
#include <pthread.h>

namespace {

using pose310b::FrameKey;
using pose310b::FrameKeyHash;
using pose310b::PoseFrame;
using pose310b::PoseResult;
using pose310b::PoseRoiPacketHeader;

constexpr uint32_t kPoseKptCount = 17;
constexpr uint32_t kPoseChannels = 4 + 1 + kPoseKptCount * 3;
constexpr uint32_t kPoseGridCount = 8400;
constexpr float kActionScoreThreshold = 0.30f;
constexpr float kKpsScoreThreshold = 0.25f;
constexpr int kActionUnknown = 0;
constexpr int kActionWave = 1;
constexpr int kActionCheer = 2;
constexpr int kDefaultDeviceId = 0;
constexpr int kDefaultBindPort = 9090;
constexpr int kDefaultResultPort = 5566;

std::atomic<bool> g_stop{false};

void on_signal(int) {
	g_stop.store(true, std::memory_order_relaxed);
}

uint64_t now_ms() {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
}

template <typename T>
class BlockingQueue {
public:
	bool push(T value) {
		std::unique_lock<std::mutex> lock(mutex_);
		if (queue_.size() >= capacity_) {
			queue_.pop_front();
		}
		queue_.push_back(std::move(value));
		lock.unlock();
		cv_.notify_one();
		return true;
	}

	bool pop(T* out) {
		std::unique_lock<std::mutex> lock(mutex_);
		cv_.wait(lock, [&] { return g_stop.load(std::memory_order_relaxed) || !queue_.empty(); });
		if (queue_.empty()) {
			return false;
		}
		*out = std::move(queue_.front());
		queue_.pop_front();
		return true;
	}

	void notify_all() {
		cv_.notify_all();
	}

private:
	static constexpr std::size_t capacity_ = 64;
	std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<T> queue_;
};

class UdpSocket {
public:
	~UdpSocket() {
		close();
	}

	bool open_and_bind(const std::string& ip, int port) {
		fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (fd_ < 0) {
			std::cerr << "[310B] socket create failed: " << std::strerror(errno) << std::endl;
			return false;
		}
		int yes = 1;
		(void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(static_cast<uint16_t>(port));
		if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
			std::cerr << "[310B] invalid bind ip: " << ip << std::endl;
			close();
			return false;
		}

		if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
			std::cerr << "[310B] bind failed " << ip << ":" << port << " err=" << std::strerror(errno) << std::endl;
			close();
			return false;
		}

		set_buffers();
		return true;
	}

	bool open_unbound() {
		fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (fd_ < 0) {
			std::cerr << "[310B] socket create failed: " << std::strerror(errno) << std::endl;
			return false;
		}
		set_buffers();
		return true;
	}

	bool set_recv_timeout_ms(int timeout_ms) {
		timeval tv{};
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
		return ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
	}

	bool recv_packet(std::vector<uint8_t>* out, sockaddr_in* src = nullptr) {
		if (!out) {
			return false;
		}
		std::array<uint8_t, 4096> buf{};
		sockaddr_in tmp_src{};
		socklen_t tmp_len = sizeof(tmp_src);
		if (!src) {
			src = &tmp_src;
		}
		int n = ::recvfrom(fd_, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(src), &tmp_len);
		if (n <= 0) {
			return false;
		}
		out->assign(buf.begin(), buf.begin() + n);
		return true;
	}

	bool send_text(const std::string& text, const sockaddr_in& dst) {
		int sent = ::sendto(fd_, text.data(), text.size(), 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
		return sent == static_cast<int>(text.size());
	}

	int fd() const {
		return fd_;
	}

	void close() {
		if (fd_ >= 0) {
			::close(fd_);
			fd_ = -1;
		}
	}

private:
	void set_buffers() {
		int buf_size = 8 * 1024 * 1024;
		(void)::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
		(void)::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
	}

	int fd_ = -1;
};

struct InflightRoi {
	PoseRoiPacketHeader hdr{};
	std::vector<uint8_t> frame_bytes;
	std::vector<uint8_t> received;
	std::vector<uint16_t> frag_sizes;
	uint16_t recv_cnt = 0;
	uint64_t start_ms = 0;
};

class RoiAssembler {
public:
	bool push_packet(const std::vector<uint8_t>& packet, PoseFrame* completed) {
		if (!completed) {
			return false;
		}

		PoseRoiPacketHeader hdr{};
		if (!pose310b::parse_packet_header(packet.data(), packet.size(), &hdr)) {
			return false;
		}

		const FrameKey key{hdr.frame_id, hdr.roi_id};
		auto it = inflight_.find(key);
		if (it == inflight_.end()) {
			cleanup_old();
			if (inflight_.size() >= kMaxInflightRois) {
				return false;
			}
			InflightRoi fresh;
			if (!init_inflight(hdr, &fresh)) {
				return false;
			}
			it = inflight_.emplace(key, std::move(fresh)).first;
		}

		InflightRoi& roi = it->second;
		if (!same_frame_meta(hdr, roi.hdr)) {
			inflight_.erase(it);
			return false;
		}
		if (hdr.frag_idx >= roi.hdr.frag_cnt) {
			return false;
		}
		if (hdr.payload_bytes > pose310b::POSE_SAFE_UDP_PAYLOAD) {
			inflight_.erase(it);
			return false;
		}

		const std::size_t payload_offset = sizeof(PoseRoiPacketHeader);
		const std::size_t write_offset = static_cast<std::size_t>(hdr.frag_idx) * pose310b::POSE_SAFE_UDP_PAYLOAD;
		if (write_offset + hdr.payload_bytes > roi.frame_bytes.size()) {
			inflight_.erase(it);
			return false;
		}

		if (!roi.received[hdr.frag_idx]) {
			std::memcpy(roi.frame_bytes.data() + write_offset, packet.data() + payload_offset, hdr.payload_bytes);
			roi.received[hdr.frag_idx] = 1;
			roi.frag_sizes[hdr.frag_idx] = hdr.payload_bytes;
			++roi.recv_cnt;
		}

		if (roi.recv_cnt == roi.hdr.frag_cnt) {
			completed->hdr = roi.hdr;
			completed->payload = std::move(roi.frame_bytes);
			inflight_.erase(it);
			return true;
		}
		return false;
	}

private:
	static constexpr std::size_t kMaxInflightRois = 16;
	static constexpr uint64_t kFrameTimeoutMs = 800;

	static bool same_frame_meta(const PoseRoiPacketHeader& a, const PoseRoiPacketHeader& b) {
		return a.frame_id == b.frame_id && a.roi_id == b.roi_id && a.frag_cnt == b.frag_cnt &&
			   a.width == b.width && a.height == b.height && a.pixel_format == b.pixel_format;
	}

	static std::size_t expected_bytes(const PoseRoiPacketHeader& hdr) {
		if (hdr.pixel_format != pose310b::POSE_PIXEL_FORMAT_NV12) {
			return 0;
		}
		return static_cast<std::size_t>(hdr.width) * static_cast<std::size_t>(hdr.height) * 3 / 2;
	}

	static bool init_inflight(const PoseRoiPacketHeader& hdr, InflightRoi* roi) {
		const std::size_t bytes = expected_bytes(hdr);
		if (bytes == 0 || hdr.frag_cnt == 0) {
			return false;
		}
		roi->hdr = hdr;
		roi->frame_bytes.assign(bytes, 0);
		roi->received.assign(hdr.frag_cnt, 0);
		roi->frag_sizes.assign(hdr.frag_cnt, 0);
		roi->start_ms = now_ms();
		return true;
	}

	void cleanup_old() {
		const uint64_t now = now_ms();
		std::vector<FrameKey> expired;
		expired.reserve(inflight_.size());
		for (const auto& kv : inflight_) {
			if (now > kv.second.start_ms && now - kv.second.start_ms > kFrameTimeoutMs) {
				expired.push_back(kv.first);
			}
		}
		for (const auto& key : expired) {
			inflight_.erase(key);
		}
	}

	std::unordered_map<FrameKey, InflightRoi, FrameKeyHash> inflight_;
};

struct PoseKeypoint {
	float x = 0.0f;
	float y = 0.0f;
	float conf = 0.0f;
};

struct PoseDetection {
	float score = 0.0f;
	std::array<float, 4> box{};
	std::array<PoseKeypoint, kPoseKptCount> kpts{};
};

struct TaskContext {
	class PoseEngine* engine = nullptr;
	std::vector<std::vector<uint8_t>> host_outputs;
	std::mutex mutex;
	std::condition_variable cv;
	bool copied = false;
};

class PoseEngine {
public:
	PoseEngine(std::string model_path, int device_id)
		: model_path_(std::move(model_path)), device_id_(device_id) {}

	~PoseEngine() {
		shutdown();
	}

	bool init() {
		if (initialized_) {
			return true;
		}

		if (aclInit(nullptr) != ACL_SUCCESS) {
			std::cerr << "[310B] aclInit failed: " << aclGetRecentErrMsg() << std::endl;
			return false;
		}

		if (aclrtSetDevice(device_id_) != ACL_SUCCESS) {
			std::cerr << "[310B] aclrtSetDevice failed" << std::endl;
			return false;
		}

		if (aclrtCreateContext(&context_, device_id_) != ACL_SUCCESS) {
			std::cerr << "[310B] aclrtCreateContext failed" << std::endl;
			return false;
		}

		if (aclrtSetCurrentContext(context_) != ACL_SUCCESS) {
			std::cerr << "[310B] aclrtSetCurrentContext failed" << std::endl;
			return false;
		}

		if (aclrtCreateStream(&stream_) != ACL_SUCCESS) {
			std::cerr << "[310B] aclrtCreateStream failed" << std::endl;
			return false;
		}

		if (aclmdlLoadFromFile(model_path_.c_str(), &model_id_) != ACL_SUCCESS) {
			std::cerr << "[310B] aclmdlLoadFromFile failed for " << model_path_ << std::endl;
			return false;
		}

		model_desc_ = aclmdlCreateDesc();
		if (!model_desc_) {
			std::cerr << "[310B] aclmdlCreateDesc failed" << std::endl;
			return false;
		}
		if (aclmdlGetDesc(model_desc_, model_id_) != ACL_SUCCESS) {
			std::cerr << "[310B] aclmdlGetDesc failed" << std::endl;
			return false;
		}

		input_count_ = aclmdlGetNumInputs(model_desc_);
		output_count_ = aclmdlGetNumOutputs(model_desc_);
		if (input_count_ == 0 || output_count_ == 0) {
			std::cerr << "[310B] model has no inputs or outputs" << std::endl;
			return false;
		}

		input_size_ = aclmdlGetInputSizeByIndex(model_desc_, 0);
		output_sizes_.resize(output_count_);
		for (size_t i = 0; i < output_count_; ++i) {
			output_sizes_[i] = aclmdlGetOutputSizeByIndex(model_desc_, i);
		}

		if (!prepare_buffers()) {
			return false;
		}

		initialized_ = true;
		std::cout << "[310B] model loaded: " << model_path_ << std::endl;
		std::cout << "[310B] input_size=" << input_size_ << " output_count=" << output_count_ << std::endl;
		return true;
	}

	bool infer(const PoseFrame& frame, PoseResult* out_result) {
		if (!initialized_ || !out_result) {
			return false;
		}

		if (aclrtSetCurrentContext(context_) != ACL_SUCCESS) {
			std::cerr << "[310B] aclrtSetCurrentContext in infer failed" << std::endl;
			return false;
		}

		if (frame.payload.size() != input_size_) {
			std::cerr << "[310B] input size mismatch frame_id=" << frame.hdr.frame_id
					  << " payload=" << frame.payload.size() << " expect=" << input_size_ << std::endl;
			return false;
		}

		TaskContext ctx;
		ctx.engine = this;
		ctx.host_outputs.resize(output_count_);
		for (size_t i = 0; i < output_count_; ++i) {
			ctx.host_outputs[i].resize(output_sizes_[i]);
		}

		if (aclrtMemcpy(input_dev_, input_size_, frame.payload.data(), input_size_, ACL_MEMCPY_HOST_TO_DEVICE) != ACL_SUCCESS) {
			std::cerr << "[310B] aclrtMemcpy H2D failed" << std::endl;
			return false;
		}

		if (aclmdlExecuteAsync(model_id_, input_dataset_, output_dataset_, stream_) != ACL_SUCCESS) {
			std::cerr << "[310B] aclmdlExecuteAsync failed" << std::endl;
			return false;
		}

		if (aclrtLaunchCallback(&PoseEngine::copy_outputs_callback, &ctx, ACL_CALLBACK_NO_BLOCK, stream_) != ACL_SUCCESS) {
			std::cerr << "[310B] aclrtLaunchCallback failed, fallback to sync wait" << std::endl;
			if (aclrtSynchronizeStream(stream_) != ACL_SUCCESS) {
				return false;
			}
			copy_outputs(ctx);
			{
				std::lock_guard<std::mutex> lock(ctx.mutex);
				ctx.copied = true;
			}
			ctx.cv.notify_one();
		}

		std::unique_lock<std::mutex> lock(ctx.mutex);
		ctx.cv.wait(lock, [&] { return ctx.copied; });

		*out_result = decode_result(frame.hdr, ctx.host_outputs);
		return true;
	}

	void start_report_loop() {
		report_thread_running_ = true;
		report_thread_ = std::thread([this] {
			if (aclrtSetCurrentContext(context_) != ACL_SUCCESS) {
				std::cerr << "[310B] aclrtSetCurrentContext in report loop failed" << std::endl;
			}
			const uint64_t thread_id = static_cast<uint64_t>(::pthread_self());
			if (aclrtSubscribeReport(thread_id, stream_) != ACL_SUCCESS) {
				std::cerr << "[310B] aclrtSubscribeReport failed" << std::endl;
			}
			while (!g_stop.load(std::memory_order_relaxed)) {
				(void)aclrtProcessReport(5);
			}
			(void)aclrtUnSubscribeReport(thread_id, stream_);
		});
	}

	void stop_report_loop() {
		if (report_thread_running_ && report_thread_.joinable()) {
			report_thread_.join();
		}
		report_thread_running_ = false;
	}

	static void copy_outputs_callback(void* user_data) {
		auto* ctx = reinterpret_cast<TaskContext*>(user_data);
		if (!ctx || !ctx->engine) {
			return;
		}
		ctx->engine->copy_outputs(*ctx);
		{
			std::lock_guard<std::mutex> lock(ctx->mutex);
			ctx->copied = true;
		}
		ctx->cv.notify_one();
	}

private:
	void shutdown() {
		stop_report_loop();

		if (output_dataset_) {
			for (size_t i = 0; i < output_buffers_.size(); ++i) {
				if (output_buffers_[i]) {
					(void)aclDestroyDataBuffer(output_buffers_[i]);
					output_buffers_[i] = nullptr;
				}
			}
			(void)aclmdlDestroyDataset(output_dataset_);
			output_dataset_ = nullptr;
		}

		if (input_dataset_) {
			if (input_buffer_) {
				(void)aclDestroyDataBuffer(input_buffer_);
				input_buffer_ = nullptr;
			}
			(void)aclmdlDestroyDataset(input_dataset_);
			input_dataset_ = nullptr;
		}

		for (void* ptr : output_dev_ptrs_) {
			if (ptr) {
				(void)aclrtFree(ptr);
			}
		}
		output_dev_ptrs_.clear();

		if (input_dev_) {
			(void)aclrtFree(input_dev_);
			input_dev_ = nullptr;
		}

		if (stream_) {
			(void)aclrtDestroyStream(stream_);
			stream_ = nullptr;
		}

		if (context_) {
			(void)aclrtDestroyContext(context_);
			context_ = nullptr;
		}

		if (device_id_ >= 0) {
			(void)aclrtResetDevice(device_id_);
		}

		if (model_id_ != 0) {
			(void)aclmdlUnload(model_id_);
			model_id_ = 0;
		}

		if (initialized_) {
			(void)aclFinalize();
			initialized_ = false;
		}
	}

	bool prepare_buffers() {
		if (aclrtMalloc(&input_dev_, input_size_, ACL_MEM_MALLOC_NORMAL_ONLY) != ACL_SUCCESS) {
			std::cerr << "[310B] aclrtMalloc input buffer failed" << std::endl;
			return false;
		}

		input_dataset_ = aclmdlCreateDataset();
		if (!input_dataset_) {
			return false;
		}
		input_buffer_ = aclCreateDataBuffer(input_dev_, input_size_);
		if (!input_buffer_) {
			return false;
		}
		if (aclmdlAddDatasetBuffer(input_dataset_, input_buffer_) != ACL_SUCCESS) {
			return false;
		}

		output_dataset_ = aclmdlCreateDataset();
		if (!output_dataset_) {
			return false;
		}

		output_dev_ptrs_.resize(output_count_);
		output_buffers_.resize(output_count_, nullptr);
		for (size_t i = 0; i < output_count_; ++i) {
			if (aclrtMalloc(&output_dev_ptrs_[i], output_sizes_[i], ACL_MEM_MALLOC_NORMAL_ONLY) != ACL_SUCCESS) {
				std::cerr << "[310B] aclrtMalloc output buffer failed index=" << i << std::endl;
				return false;
			}
			output_buffers_[i] = aclCreateDataBuffer(output_dev_ptrs_[i], output_sizes_[i]);
			if (!output_buffers_[i]) {
				return false;
			}
			if (aclmdlAddDatasetBuffer(output_dataset_, output_buffers_[i]) != ACL_SUCCESS) {
				return false;
			}
		}
		return true;
	}

	void copy_outputs(TaskContext& ctx) {
		for (size_t i = 0; i < output_count_; ++i) {
			(void)aclrtMemcpy(ctx.host_outputs[i].data(), ctx.host_outputs[i].size(),
							  output_dev_ptrs_[i], ctx.host_outputs[i].size(), ACL_MEMCPY_DEVICE_TO_HOST);
		}
	}

	static float read_tensor_value(const uint8_t* base, size_t index, bool fp16) {
		if (fp16) {
			const auto* ptr = reinterpret_cast<const aclFloat16*>(base);
			return aclFloat16ToFloat(ptr[index]);
		}
		const auto* ptr = reinterpret_cast<const float*>(base);
		return ptr[index];
	}

	static PoseDetection decode_best_detection(const std::vector<uint8_t>& raw) {
		PoseDetection best{};
		if (raw.empty()) {
			return best;
		}

		const bool fp16 = (raw.size() == kPoseChannels * kPoseGridCount * sizeof(aclFloat16));
		const size_t elem_size = fp16 ? sizeof(aclFloat16) : sizeof(float);
		if (raw.size() % elem_size != 0) {
			return best;
		}

		const size_t total_elems = raw.size() / elem_size;
		if (total_elems < kPoseChannels) {
			return best;
		}

		const size_t grid_count = total_elems / kPoseChannels;
		const uint8_t* base = raw.data();

		float best_score = 0.0f;
		for (size_t i = 0; i < grid_count; ++i) {
			const float x = read_tensor_value(base, 0 * grid_count + i, fp16);
			const float y = read_tensor_value(base, 1 * grid_count + i, fp16);
			const float w = read_tensor_value(base, 2 * grid_count + i, fp16);
			const float h = read_tensor_value(base, 3 * grid_count + i, fp16);
			const float score = read_tensor_value(base, 4 * grid_count + i, fp16);
			if (score < best_score || score < kActionScoreThreshold) {
				continue;
			}

			PoseDetection candidate;
			candidate.score = score;
			candidate.box = {x, y, w, h};
			for (size_t k = 0; k < kPoseKptCount; ++k) {
				candidate.kpts[k].x = read_tensor_value(base, (5 + k * 3 + 0) * grid_count + i, fp16);
				candidate.kpts[k].y = read_tensor_value(base, (5 + k * 3 + 1) * grid_count + i, fp16);
				candidate.kpts[k].conf = read_tensor_value(base, (5 + k * 3 + 2) * grid_count + i, fp16);
			}
			best = candidate;
			best_score = score;
		}
		return best;
	}

	static float keypoint_conf_mean(const PoseDetection& det) {
		float sum = 0.0f;
		int count = 0;
		for (const auto& kpt : det.kpts) {
			if (kpt.conf > 0.0f) {
				sum += kpt.conf;
				++count;
			}
		}
		return count > 0 ? sum / static_cast<float>(count) : 0.0f;
	}

	static std::pair<int, float> classify_action(const PoseDetection& det) {
		const PoseKeypoint& left_shoulder = det.kpts[5];
		const PoseKeypoint& right_shoulder = det.kpts[6];
		const PoseKeypoint& left_wrist = det.kpts[9];
		const PoseKeypoint& right_wrist = det.kpts[10];

		const float left_support = std::min(left_shoulder.conf, left_wrist.conf);
		const float right_support = std::min(right_shoulder.conf, right_wrist.conf);
		if (left_support < kKpsScoreThreshold && right_support < kKpsScoreThreshold) {
			return {kActionUnknown, 0.0f};
		}

		const bool left_up = left_wrist.conf > kKpsScoreThreshold && left_shoulder.conf > kKpsScoreThreshold &&
							 left_wrist.y < left_shoulder.y - 0.03f;
		const bool right_up = right_wrist.conf > kKpsScoreThreshold && right_shoulder.conf > kKpsScoreThreshold &&
							  right_wrist.y < right_shoulder.y - 0.03f;

		if (left_up && right_up) {
			return {kActionCheer, std::min({left_wrist.conf, right_wrist.conf, left_shoulder.conf, right_shoulder.conf})};
		}
		if (left_up || right_up) {
			return {kActionWave, std::max(left_wrist.conf, right_wrist.conf)};
		}
		return {kActionUnknown, std::max(left_support, right_support)};
	}

	PoseResult decode_result(const PoseRoiPacketHeader& hdr, const std::vector<std::vector<uint8_t>>& outputs) {
		PoseResult result{};
		result.frame_id = hdr.frame_id;
		result.timestamp_ms = hdr.timestamp_ms;
		result.roi_id = hdr.roi_id;
		result.roi_index = hdr.roi_index;

		if (outputs.empty()) {
			return result;
		}

		PoseDetection best = decode_best_detection(outputs[0]);
		result.kps_conf_mean = keypoint_conf_mean(best);
		auto action = classify_action(best);
		result.action_flag = action.first;
		result.action_conf = action.second;
		return result;
	}

	std::string model_path_;
	int device_id_ = 0;
	aclrtContext context_ = nullptr;
	aclrtStream stream_ = nullptr;
	uint32_t model_id_ = 0;
	aclmdlDesc* model_desc_ = nullptr;
	aclmdlDataset* input_dataset_ = nullptr;
	aclmdlDataset* output_dataset_ = nullptr;
	aclDataBuffer* input_buffer_ = nullptr;
	std::vector<aclDataBuffer*> output_buffers_;
	void* input_dev_ = nullptr;
	std::vector<void*> output_dev_ptrs_;
	size_t input_count_ = 0;
	size_t output_count_ = 0;
	size_t input_size_ = 0;
	std::vector<size_t> output_sizes_;
	std::thread report_thread_;
	bool report_thread_running_ = false;
	bool initialized_ = false;
};

std::string build_result_json(const PoseResult& result) {
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(4);
	oss << "{"
		<< "\"frame_id\":" << result.frame_id << ','
		<< "\"timestamp_ms\":" << result.timestamp_ms << ','
		<< "\"kps_conf_mean\":" << result.kps_conf_mean << ','
		<< "\"action_flag\":" << result.action_flag << ','
		<< "\"action_conf\":" << result.action_conf
		<< "}";
	return oss.str();
}

struct AppConfig {
	std::string model_path = "yolo26/yolo26s-pose.om";
	std::string bind_ip = "192.168.2.2";
	int bind_port = kDefaultBindPort;
	std::string peer_ip = "192.168.137.1";
	int peer_port = kDefaultResultPort;
	int device_id = kDefaultDeviceId;
	
	// IP 视频联动配置
	std::string video_pc_ip = "192.168.137.1";
	int video_port = ascend310b::IpVideoPlayer::kDefaultVideoPort;
	std::string videos_dir = "/home/data/videos";  // 预生成视频目录
};

AppConfig parse_args(int argc, char** argv) {
	AppConfig cfg;
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		auto next = [&](std::string* out) {
			if (i + 1 < argc) {
				*out = argv[++i];
			}
		};
		auto next_int = [&](int* out) {
			if (i + 1 < argc) {
				*out = std::stoi(argv[++i]);
			}
		};
		if (arg == "--model") {
			next(&cfg.model_path);
		} else if (arg == "--bind-ip") {
			next(&cfg.bind_ip);
		} else if (arg == "--bind-port") {
			next_int(&cfg.bind_port);
		} else if (arg == "--peer-ip") {
			next(&cfg.peer_ip);
		} else if (arg == "--peer-port") {
			next_int(&cfg.peer_port);
		} else if (arg == "--device") {
			next_int(&cfg.device_id);
		} else if (arg == "--video-pc-ip") {
			next(&cfg.video_pc_ip);
		} else if (arg == "--video-port") {
			next_int(&cfg.video_port);
		} else if (arg == "--videos-dir") {
			next(&cfg.videos_dir);
		}
	}
	return cfg;
}

} // namespace

int main(int argc, char** argv) {
	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);

	AppConfig cfg = parse_args(argc, argv);
	std::cout << "[310B] model=" << cfg.model_path << " bind=" << cfg.bind_ip << ':' << cfg.bind_port
			  << " peer=" << cfg.peer_ip << ':' << cfg.peer_port << std::endl;
	std::cout << "[310B] video_pc=" << cfg.video_pc_ip << ":" << cfg.video_port 
			  << " videos_dir=" << cfg.videos_dir << std::endl;

	UdpSocket rx;
	if (!rx.open_and_bind(cfg.bind_ip, cfg.bind_port)) {
		return 1;
	}
	(void)rx.set_recv_timeout_ms(20);

	UdpSocket tx;
	if (!tx.open_unbound()) {
		return 1;
	}
	sockaddr_in peer{};
	peer.sin_family = AF_INET;
	peer.sin_port = htons(static_cast<uint16_t>(cfg.peer_port));
	if (::inet_pton(AF_INET, cfg.peer_ip.c_str(), &peer.sin_addr) <= 0) {
		std::cerr << "[310B] invalid peer ip: " << cfg.peer_ip << std::endl;
		return 1;
	}

	PoseEngine engine(cfg.model_path, cfg.device_id);
	if (!engine.init()) {
		return 1;
	}
	engine.start_report_loop();

	// 初始化 IP 视频播放器
	ascend310b::IpVideoPlayer video_player(cfg.video_pc_ip, cfg.video_port);
	if (!video_player.init()) {
		std::cerr << "[310B] failed to init video player" << std::endl;
		return 1;
	}
	
	// 注册预生成的视频资产
	// 挥手动作视频 (action_flag=1)
	video_player.registerVideo(ascend310b::IpVideoPlayer::kActionWave, cfg.videos_dir + "/wave1.mp4", 0);
	video_player.registerVideo(ascend310b::IpVideoPlayer::kActionWave, cfg.videos_dir + "/wave2.mp4", 1);
	// 欢呼动作视频 (action_flag=2)
	video_player.registerVideo(ascend310b::IpVideoPlayer::kActionCheer, cfg.videos_dir + "/heart1.mp4", 0);

	BlockingQueue<PoseFrame> frame_queue;

	std::thread recv_thread([&] {
		RoiAssembler assembler;
		while (!g_stop.load(std::memory_order_relaxed)) {
			std::vector<uint8_t> packet;
			sockaddr_in src{};
			if (!rx.recv_packet(&packet, &src)) {
				continue;
			}
			PoseFrame frame;
			if (assembler.push_packet(packet, &frame)) {
				frame_queue.push(std::move(frame));
			}
		}
		frame_queue.notify_all();
	});

	std::thread infer_thread([&] {
		while (!g_stop.load(std::memory_order_relaxed)) {
			PoseFrame frame;
			if (!frame_queue.pop(&frame)) {
				break;
			}

			PoseResult result;
			const auto infer_start = std::chrono::steady_clock::now();
			if (!engine.infer(frame, &result)) {
				continue;
			}
			const auto infer_end = std::chrono::steady_clock::now();
			const auto infer_us = std::chrono::duration_cast<std::chrono::microseconds>(infer_end - infer_start).count();

			const std::string json = build_result_json(result);
			if (!tx.send_text(json, peer)) {
				std::cerr << "[310B] result send failed frame_id=" << result.frame_id << std::endl;
			} else {
				std::cout << "[310B] frame_id=" << result.frame_id
						  << " roi=" << result.roi_index
						  << " kps_conf_mean=" << std::fixed << std::setprecision(4) << result.kps_conf_mean
						  << " action_flag=" << result.action_flag
						  << " action_conf=" << result.action_conf
						  << " infer_us=" << infer_us << std::endl;
				
				// 触发动作对应的视频播放（仅当检测到有效动作时）
				if (result.action_flag != kActionUnknown && result.action_conf >= kActionScoreThreshold) {
					video_player.triggerAction(result.action_flag, result.action_conf, result.frame_id);
				}
			}
		}
	});

	while (!g_stop.load(std::memory_order_relaxed)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	frame_queue.notify_all();
	if (recv_thread.joinable()) {
		recv_thread.join();
	}
	if (infer_thread.joinable()) {
		infer_thread.join();
	}
	engine.stop_report_loop();
	std::cout << "[310B] shutdown" << std::endl;
	return 0;
}
