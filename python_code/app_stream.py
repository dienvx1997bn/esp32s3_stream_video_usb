import serial
import sys
import time
from PIL import Image
import pyautogui
import io

def capture_screen_region(x=0, y=0, width=480, height=480):
    """Chụp một phần màn hình tại tọa độ (x, y) với kích thước width x height."""
    screenshot = pyautogui.screenshot(region=(x, y, width, height))
    return screenshot

def resize_image(image, target_width=240, target_height=240):
    """Resize ảnh về kích thước target_width x target_height."""
    resized_image = image.resize((target_width, target_height), Image.Resampling.LANCZOS)
    return resized_image

def convert_to_jpeg(image):
    """Chuyển đổi ảnh PIL thành JPEG và trả về dữ liệu byte."""
    buffer = io.BytesIO()
    image.save(buffer, format="JPEG", quality=85)
    jpeg_data = buffer.getvalue()
    buffer.close()
    return jpeg_data

def send_jpeg_over_usb(ser, jpeg_data):
    """Gửi dữ liệu JPEG qua USB với header."""
    total_size = len(jpeg_data)
    header = bytes([0xAA, 0xBB]) + total_size.to_bytes(4, 'big')
    print(f"Sending Header: {[hex(x) for x in header]}")
    ser.write(header)
    ser.flush()

    print(f"Sending JPEG, size: {total_size} bytes")
    ser.write(jpeg_data)
    ser.flush()
    print(f"Sent {total_size} bytes over USB")

is_first = True
def wait_for_ready(ser, timeout=5):
    """Chờ tín hiệu 'ready' từ ESP, đồng thời chụp và chuẩn bị ảnh."""
    print("Waiting for 'ready' signal from ESP and preparing next image...")
    start_time = time.time()
    global is_first

    # Chụp và chuẩn bị ảnh trong khi chờ
    screen_region = capture_screen_region(x=0, y=0, width=480, height=480)
    resized_image = resize_image(screen_region, target_width=240, target_height=240)
    jpeg_data = convert_to_jpeg(resized_image)
    print(f"Image prepared, size: {len(jpeg_data)} bytes")

    while time.time() - start_time < timeout:
        line = ser.readline().decode('utf-8').strip()
        if line == "ready" or is_first:
            is_first = False
            print("Received 'ready' signal from ESP")
            return jpeg_data  # Trả về ảnh đã chuẩn bị sẵn
        # time.sleep(0.1)
    print("Timeout waiting for 'ready' signal")
    return None  # Trả về None nếu timeout

def main():
    if len(sys.argv) < 2:
        print("Please enter COM port (e.g., python script.py COM3)")
        sys.exit(1)
    
    com_port = sys.argv[1]
    ser = serial.Serial(com_port, 921600, timeout=1)
    print(f"Connected to {com_port}")

    try:
        while True:
            # Chờ tín hiệu "ready" và chụp ảnh trước
            jpeg_data = wait_for_ready(ser)

            if jpeg_data is not None:
                # Gửi ảnh ngay khi nhận được "ready"
                send_jpeg_over_usb(ser, jpeg_data)
            else:
                print("ESP not responding, retrying...")

    except KeyboardInterrupt:
        print("Stopped by user")
    finally:
        ser.close()
        print("Serial port closed")

if __name__ == "__main__":
    main()