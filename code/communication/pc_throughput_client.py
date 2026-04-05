import socket, time

SERVER_IP = '192.168.2.99'
PORT = 5201
DURATION = 10
PAYLOAD = b'X' * (256 * 1024)  # 256KB/块

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4 * 1024 * 1024)
sock.connect((SERVER_IP, PORT))
print(f"📤 Python iperf 客户端: 发送 {DURATION} 秒...")

start = time.perf_counter()
sent = 0
while (time.perf_counter() - start) < DURATION:
    sock.sendall(PAYLOAD)
    sent += len(PAYLOAD)

sock.shutdown(socket.SHUT_WR)
end = time.perf_counter()
throughput_mbps = (sent * 8) / ((end - start) * 1e6)
print(f"\n📊 发送: {sent/1e6:.2f} MB | {end-start:.3f}s | 🚀 {throughput_mbps:.2f} Mbps")
sock.close()