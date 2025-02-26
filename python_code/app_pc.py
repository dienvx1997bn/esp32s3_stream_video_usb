import serial
import sys
import os
from time import sleep

def send_jpeg_over_usb(ser, file_path):
    with open(file_path, 'rb') as f:
        jpeg_data = f.read()
        total_size = len(jpeg_data)
        
        # Gửi Header: [0xAA 0xBB Size (4 bytes)]
        header = bytes([0xAA, 0xBB]) + total_size.to_bytes(4, 'big')
        print(f"Sending Header: {[hex(x) for x in header]} total byte {total_size}")
        ser.write(header)
        # ser.flush()
        # sleep(0.1)

        # Gửi dữ liệu JPEG
        print(f"Sending {file_path}, size: {total_size} bytes")
        ser.write(jpeg_data)
        # ser.flush()
        print(f"Sent {total_size} bytes over USB")

def main():
    if len(sys.argv) < 2:
        print("Please enter COM port (e.g., python script.py COM3)")
        sys.exit(1)
    
    com_port = sys.argv[1]
    ser = serial.Serial(com_port, 921600, timeout=1)
    print(f"Connected to {com_port}")

    folder_path = "gif_images"
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
        while True:
            for filename in jpeg_files:
                file_path = os.path.join(folder_path, filename)
                send_jpeg_over_usb(ser, file_path)
                sleep(0.2)  # Delay giữa các file
    except KeyboardInterrupt:
        print("Stopped by user")
    finally:
        ser.close()

if __name__ == "__main__":
    main()