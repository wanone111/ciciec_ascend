import zmq
import time
import statistics

SERVER_IP = '192.168.2.99'
PORT = 5555
COUNT = 500
PAYLOAD_SIZE = 256  # 字节 (模拟控制信令/小数据包)
PAYLOAD = b'Z' * PAYLOAD_SIZE

context = zmq.Context()
socket = context.socket(zmq.REQ)
socket.connect(f"tcp://{SERVER_IP}:{PORT}")
socket.setsockopt(zmq.LINGER, 0)
socket.setsockopt(zmq.RCVTIMEO, 2000)
socket.setsockopt(zmq.SNDTIMEO, 2000)

print(f"📡 向 {SERVER_IP}:{PORT} 发送 {COUNT} 个 {PAYLOAD_SIZE}B 探测包...")
latencies = []

for i in range(COUNT):
    t0 = time.perf_counter()
    try:
        socket.send(PAYLOAD)
        socket.recv()
        rtt_ms = (time.perf_counter() - t0) * 1000
        latencies.append(rtt_ms)
    except Exception as e:
        print(f"  [{i+1}] ⚠️ 超时/错误: {e}")

if latencies:
    print("\n📊 延迟统计 (RTT):")
    print(f"  最小: {min(latencies):.3f} ms | 最大: {max(latencies):.3f} ms")
    print(f"  平均: {statistics.mean(latencies):.3f} ms | 中位: {statistics.median(latencies):.3f} ms")
    print(f"  抖动: {statistics.pstdev(latencies):.3f} ms")
    print(f"  💡 估算单向延迟: ~{statistics.mean(latencies)/2:.3f} ms")

socket.close()
context.term()