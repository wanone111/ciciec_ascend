import cv2
import socket
import time

# 目标 310B 的 IP 和端口
TARGET_IP = "192.168.1.100"  # 请替换为你的 310B 实际 IP
TARGET_PORT = 8080

def start_sender():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cap = cv2.VideoCapture(0)  # 打开本地摄像头，如果是测试可以用视频文件代替
    
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

    print(f"开始向 {TARGET_IP}:{TARGET_PORT} 发送视频流...")
    
    while True:
        ret, frame = cap.read()
        if not ret:
            break
            
        # 为了适应 UDP 限制，先用 JPEG 压缩 (质量设为 80)
        _, encode_param = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 80])
        data = encode_param.tobytes()
        
        try:
            # 发送数据
            sock.sendto(data, (TARGET_IP, TARGET_PORT))
        except Exception as e:
            print(f"发送失败: {e}，数据大小: {len(data)} bytes")
            
        time.sleep(0.03) # 模拟 ~30FPS

    cap.release()
    sock.close()

if __name__ == "__main__":
    start_sender()