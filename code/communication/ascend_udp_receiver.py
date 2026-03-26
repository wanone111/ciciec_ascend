import socket
import cv2
import numpy as np
import time

LISTEN_IP = "0.0.0.0" # 监听所有网卡
LISTEN_PORT = 8080

def start_receiver():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LISTEN_IP, LISTEN_PORT))
    
    print(f"等待接收数据，端口: {LISTEN_PORT}...")
    
    # 接收缓冲区设大一点 (64KB)
    buffer_size = 65536 

    while True:
        try:
            data, addr = sock.recvfrom(buffer_size)
            
            # 记录接收时间，可用于评估延迟
            # print(f"收到来自 {addr} 的数据，大小: {len(data)} bytes")
            
            # 解码 JPEG
            frame_data = np.frombuffer(data, dtype=np.uint8)
            frame = cv2.imdecode(frame_data, cv2.IMREAD_COLOR)
            
            if frame is not None:
                # 310B 如果没有桌面环境，可以把这里改成保存图片或者直接送入 AI 模型
                # cv2.imshow('310B Receiver', frame)
                # if cv2.waitKey(1) & 0xFF == ord('q'):
                #     break
                pass
                
        except Exception as e:
            print(f"接收出错: {e}")

if __name__ == "__main__":
    start_receiver()