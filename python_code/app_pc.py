

import serial
import sys
import time
from array import array
import io
import os
from time import sleep
from PIL import Image

BYTE_PER_MESSAGE = 252

def split_data_to_fit_size(data):
    loop_count = int(len(data) / BYTE_PER_MESSAGE)
    remainder = len(data) % BYTE_PER_MESSAGE
    byte_added = 0
    data_to_send = []
    i = 0
    j = 0

    # normal data
    while i < loop_count:
        # print(f"i {i} loop_count {loop_count}")
        my_array = [i & 255, (i >> 8) & 255, BYTE_PER_MESSAGE, 00]
        for _ in range(BYTE_PER_MESSAGE):
            my_array.append(data[byte_added])
            byte_added += 1
        data_to_send.append(my_array)
        i += 1
    
    # last data
    if(remainder > 0):
        my_array = [i & 255, i >> 8, 0, remainder]
        while j < remainder:
            my_array.append(data[byte_added])
            byte_added += 1
            j+= 1
        data_to_send.append(my_array)

    return data_to_send


def split_list_by_byte_size(data_list, chunk_size=64):
    """Splits a list into chunks of a specified byte size.

    Args:
        data_list: The list to be split.
        chunk_size: The desired byte size of each chunk.

    Returns:
        A list of chunks, where each chunk is a list of elements.
    """

    chunks = []
    current_chunk = []
    current_chunk_size = 0

    for item in data_list:
        item_size = len(str(item).encode('utf-8'))  # Assuming UTF-8 encoding
        if current_chunk_size + item_size <= chunk_size:
            current_chunk.append(item)
            current_chunk_size += item_size
        else:
            chunks.append(current_chunk)
            current_chunk = [item]
            current_chunk_size = item_size

    if current_chunk:
        chunks.append(current_chunk)

    return chunks



def send_data_to_com_port(port, data):
    # data_bytes = bytes(data)
    # data = data[0]
    # print(f"data: {data[0]} {data[1]} {data[2]} {data[3]} {data[4]} {data[5]}")
    # values = bytearray(data)
    try:
        global ser
        ser.write(data)  # Chuyển đổi dữ liệu thành bytes và gửi
        # sleep(0.1)
        # print("Dữ liệu đã được gửi đến cổng:", port)
    except Exception as e:
        print("Error while sending data: ", e)

def test():
    pixel = []
    for _ in range(240*240*2):
        pixel.append(0xAA)
    data_to_send = split_data_to_fit_size(pixel)
    split_data = split_list_by_byte_size(data_to_send, 512)
    # print(f"split_data {split_data}")
    for chunk in split_data:
        if(len(chunk) > 0):
            send_data_to_com_port(com_port, chunk[0])
        # sleep(5)
    # time.sleep(2)
    # sleep(0.1)


def rgb_to_uint16(rgb_tuple):
    """
    Convert an RGB tuple (R, G, B) to a single 16-bit integer representation.
    Uses 5 bits for red, 6 bits for green, and 5 bits for blue.
    """
    r, g, b = rgb_tuple
    
    # Reduce RGB channels to fit into 16 bits
    r = (r >> 3) & 0x1F  # 5 bits for red
    g = (g >> 2) & 0x3F  # 6 bits for green
    b = (b >> 3) & 0x1F  # 5 bits for blue
    
    # Combine into a single 16-bit integer
    return (r << 11) | (g << 5) | b

def uint16_to_uint8_pair(uint16_value):
    """
    Convert a 16-bit integer to two 8-bit integers (high byte and low byte).
    """
    high_byte = (uint16_value >> 8) & 0xFF  # Get the upper 8 bits
    low_byte = uint16_value & 0xFF  # Get the lower 8 bits
    return [high_byte, low_byte]

def jpg_files_to_byte_arrays(folder_path):
    # Initialize a list to store the byte arrays
    byte_arrays = []
    file_list = os.listdir(folder_path)
    sorted_files = sorted(file_list, key=lambda x: os.path.getmtime(os.path.join(folder_path, x)), reverse=False)
    # Loop through each file in the folder
    for filename in sorted_files:
        # Process only files with .jpg extension
        if filename.endswith(".jpg"):
            file_path = os.path.join(folder_path, filename)
            
            # Open the image
            with Image.open(file_path) as img:
                # Convert the image to a byte array
                img = img.convert("RGB")  # Save image to the in-memory byte stream
                rgb_values = list(img.getdata())  # Get the byte content
                # uint16_values = [rgb_to_uint16(rgb) for rgb in byte_array]
                # uint8_lists = [list(uint16_to_uint8_pair(rgb_to_uint16(rgb))) for rgb in rgb_values]
                # uint8_lists = [item for tup in uint8_lists for item in tup]
                uint8_lists = []
                rgb565_values = [rgb_to_uint16(rgb) for rgb in rgb_values]
                for value in rgb565_values:
                    uint8_lists.append(value & 255)
                    uint8_lists.append((value >> 8) & 255)

                # Append byte array to the list
                byte_arrays.append(uint8_lists)
                print(f"Converted {filename} to byte array")
    return byte_arrays

def send_image_over_serial(byte_array):
    # print(f"byte_array len {len(byte_array)}")
    data_to_send = split_data_to_fit_size(byte_array)
    # print(f"data_to_send {data_to_send}")
    split_data = split_list_by_byte_size(data_to_send, 512)
    # print(f"split_data {split_data}")
    for chunk in split_data:
        # print(f"chunk {chunk}")
        if(len(chunk) > 0):
            send_data_to_com_port(com_port, chunk[0])


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Please enter COM port number")
        sys.exit(1)
    com_port = sys.argv[1]

    global ser
    ser = serial.Serial(com_port, 115200)  # Tạo đối tượng serial, bạn có thể thay đổi baudrate nếu cần

    # test()
    byte_arrays = jpg_files_to_byte_arrays("cartoon")

    # byte_array = byte_arrays[0]
    # send_image_over_serial(byte_array)
    
    while True:
        for byte_array in byte_arrays:
            send_image_over_serial(byte_array)

    ser.close()