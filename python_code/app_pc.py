import serial
import sys
import os
from time import sleep

def send_jpeg_over_usb(ser, file_path):
    with open(file_path, 'rb') as f:
        jpeg_data = f.read()
        total_size = len(jpeg_data)
        
        header = bytes([0xAA, 0xBB]) + total_size.to_bytes(4, 'big')
        print(f"Sending Header: {[hex(x) for x in header]}")
        ser.write(header)
        ser.flush()

        print(f"Sending {file_path}, size: {total_size} bytes")
        ser.write(jpeg_data)
        ser.flush()
        print(f"Sent {total_size} bytes over USB")

def wait_for_ready(ser):
    # print("Waiting for 'ready' signal from ESP...")
    while True:
        line = ser.readline().decode('utf-8').strip()
        if line == "ready":
            # print("Received 'ready' signal from ESP")
            return True
        sleep(0.1)  # Ngắn để tránh delay quá lâu

def main():
    if len(sys.argv) < 2:
        print("Please enter COM port (e.g., python script.py COM3)")
        sys.exit(1)
    
    com_port = sys.argv[1]
    ser = serial.Serial(com_port, 921600, timeout=1)
    print(f"Connected to {com_port}")

    folder_path = "cartoon"
    if not os.path.exists(folder_path):
        print("Folder 'images' not found")
        ser.close()
        sys.exit(1)

    jpeg_files = [f for f in os.listdir(folder_path) if f.endswith(".jpg")]
    if not jpeg_files:
        print("No JPG files found in 'images' folder")
        ser.close()
        sys.exit(1)

    try:
        index = 0
        while True:
            # Gửi file JPEG tiếp theo
            file_path = os.path.join(folder_path, jpeg_files[index])
            send_jpeg_over_usb(ser, file_path)

            # Tăng index, quay lại đầu danh sách nếu hết
            index = (index + 1) % len(jpeg_files)

            # Chờ tín hiệu "ready" từ ESP
            wait_for_ready(ser)
    except KeyboardInterrupt:
        print("Stopped by user")
    finally:
        ser.close()

if __name__ == "__main__":
    main()