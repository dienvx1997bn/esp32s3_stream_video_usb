# esp32s3_stream_video_usb
This example to stream video over built-in usb of esp32s3

Still in development

Current limit is 20 fps, achieved with small image sizes and a comparison algorithm to minimize unnecessary screen sending when there is not much change between frames

# Hard ware connection
ESP32S3 + ST7789 240x240. SPI connection

# How to run
On ESP32:
Using esp-idf 5.3
USB connection, e.g port 'COM7'

On PC:
python .\app_stream.py COM7

# TODO
Optimize JPEG decoding
Optimize draw LCD ST7789

# Performance test
<img src="https://raw.githubusercontent.com/dienvx1997bn/esp32s3_stream_video_usb/refs/heads/main/fps_test.png">