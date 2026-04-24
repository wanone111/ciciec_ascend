from pynq import Overlay, allocate
import cv2
import numpy as np
import time
import json
import socket
import os

BIT = "/home/xilinx/jupyter_notebooks/roi_stat/design_1.bit"

H, W = 480, 640
ROI_X, ROI_Y, ROI_W, ROI_H = 280, 180, 80, 80

UDP_ENABLE = False
TARGET = ("192.168.1.100", 5005)
PRINT_EVERY_N_FRAMES = 30


class CpuMonitor:
    def __init__(self):
        self.clk_tck = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
        self.last_total, self.last_cores = self._read_cpu_stat()
        self.last_proc = self._read_proc_stat()
        self.last_wall = time.time()

    def _parse_cpu_line(self, line: str):
        parts = line.strip().split()
        vals = list(map(int, parts[1:]))
        idle = vals[3] + (vals[4] if len(vals) > 4 else 0)
        total = sum(vals)
        return total, idle

    def _read_cpu_stat(self):
        with open("/proc/stat", "r") as f:
            lines = f.readlines()

        total = self._parse_cpu_line(lines[0])
        cores = []
        for line in lines[1:]:
            if line.startswith("cpu") and len(line) > 3 and line[3].isdigit():
                cores.append(self._parse_cpu_line(line))
        return total, cores

    def _read_proc_stat(self):
        with open("/proc/self/stat", "r") as f:
            parts = f.read().split()
        utime = int(parts[13])
        stime = int(parts[14])
        return utime + stime

    def _usage_pct(self, cur, last):
        total_delta = cur[0] - last[0]
        idle_delta = cur[1] - last[1]
        if total_delta <= 0:
            return 0.0
        busy = total_delta - idle_delta
        return 100.0 * busy / total_delta

    def sample(self):
        cur_total, cur_cores = self._read_cpu_stat()
        cur_proc = self._read_proc_stat()
        cur_wall = time.time()

        total_cpu = self._usage_pct(cur_total, self.last_total)
        core_cpu = []
        for i in range(min(len(cur_cores), len(self.last_cores))):
            core_cpu.append(self._usage_pct(cur_cores[i], self.last_cores[i]))

        wall_delta = cur_wall - self.last_wall
        proc_delta = cur_proc - self.last_proc
        if wall_delta <= 0:
            proc_cpu = 0.0
        else:
            proc_cpu = 100.0 * (proc_delta / self.clk_tck) / wall_delta

        self.last_total = cur_total
        self.last_cores = cur_cores
        self.last_proc = cur_proc
        self.last_wall = cur_wall

        return total_cpu, core_cpu, proc_cpu


print("[INFO] 加载 Overlay")
ol = Overlay(BIT)
ip = ol.roi_stat_top_0
rm = ip.register_map

print("Loaded overlay.")
print("IPs:", list(ol.ip_dict.keys()))
print("==== register map ====")
print(rm)
print("======================")

buf = allocate(shape=(H, W, 3), dtype=np.uint8)

cap = cv2.VideoCapture("/dev/video0", cv2.CAP_V4L2)
if not cap.isOpened():
    cap = cv2.VideoCapture("/dev/video0")
if not cap.isOpened():
    raise RuntimeError("无法打开 /dev/video0")

cap.set(cv2.CAP_PROP_FRAME_WIDTH, W)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, H)
cap.set(cv2.CAP_PROP_FPS, 30)

real_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
real_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
real_fps = cap.get(cv2.CAP_PROP_FPS)

print("========== 摄像头信息 ==========")
print(f"shape        : ({real_h}, {real_w}, 3)")
print(f"driver fps   : {real_fps}")
print("================================")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) if UDP_ENABLE else None
cpu_mon = CpuMonitor()

def write_img_addr(rm_obj, addr):
    low = addr & 0xffffffff
    high = (addr >> 32) & 0xffffffff

    # 适配你当前的寄存器映射
    rm_obj.img_1 = low
    rm_obj.img_2 = high

def start_ip(rm_obj, buf_addr):
    rm_obj.rows = H
    rm_obj.cols = W
    rm_obj.stride_bytes = W * 3
    rm_obj.roi_x = ROI_X
    rm_obj.roi_y = ROI_Y
    rm_obj.roi_w = ROI_W
    rm_obj.roi_h = ROI_H
    write_img_addr(rm_obj, buf_addr)
    rm_obj.CTRL.AP_START = 1

def wait_done(rm_obj, timeout_s=1.0):
    t0 = time.time()
    while rm_obj.CTRL.AP_DONE == 0:
        if time.time() - t0 > timeout_s:
            raise TimeoutError("等待 AP_DONE 超时")

# 首帧预热
ok, frame = cap.read()
if not ok or frame is None:
    raise RuntimeError("首帧读取失败")

if frame.shape[:2] != (H, W):
    raise RuntimeError(f"摄像头实际帧尺寸为 {frame.shape}，与设定 {(H, W)} 不一致")

buf[:] = frame
buf.sync_to_device()
start_ip(rm, buf.device_address)
wait_done(rm)

print("[INFO] 首帧预热完成")

frame_id = 0
t0 = time.time()

try:
    while True:
        ok, frame = cap.read()
        if not ok or frame is None:
            continue

        buf[:] = frame
        buf.sync_to_device()

        start_ip(rm, buf.device_address)
        wait_done(rm)

        brightness = int(rm.brightness_mean)
        motion = int(rm.motion_score)

        if brightness < 40:
            state = "DARK"
        elif motion >= 4:
            state = "ACTIVE"
        else:
            state = "IDLE"

        frame_id += 1
        fps = frame_id / (time.time() - t0)

        if frame_id % PRINT_EVERY_N_FRAMES == 0:
            total_cpu, core_cpu, proc_cpu = cpu_mon.sample()
            payload = {
                "frame_id": frame_id,
                "brightness": brightness,
                "motion": motion,
                "state": state,
                "avg_fps": round(fps, 2),
                "ps_cpu_total": round(total_cpu, 2),
                "ps_cpu_core0": round(core_cpu[0], 2) if len(core_cpu) > 0 else None,
                "ps_cpu_core1": round(core_cpu[1], 2) if len(core_cpu) > 1 else None,
                "python_proc_cpu": round(proc_cpu, 2),
            }

            print(payload)

            if sock is not None:
                sock.sendto(json.dumps(payload).encode("utf-8"), TARGET)

except KeyboardInterrupt:
    print("\n[INFO] 用户中断，程序结束")

finally:
    cap.release()
    buf.freebuffer()
    if sock is not None:
        sock.close()
    print("[INFO] 资源已释放")