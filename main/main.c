#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "driver/gpio.h"

#include "st7789.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "sdkconfig.h"
#include "jpeg_decoder.h" // Thư viện JPEG từ ESP-IDF

#ifndef CONFIG_TINYUSB_CDC_RX_BUFSIZE
#define CONFIG_TINYUSB_CDC_RX_BUFSIZE 512
#endif

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240
#define IMAGE_BUFFER_SIZE  (SCREEN_WIDTH * SCREEN_HEIGHT * 2) // 115200 bytes for RGB565
#define RING_BUFFER_SIZE   (32 * 1024) // Buffer cho JPEG


static const char *TAG = "TEST";

typedef struct {
    uint8_t buffer[RING_BUFFER_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} RingBuffer_t;

RingBuffer_t ring_buffer = { .head = 0, .tail = 0, .count = 0 };
TFT_t dev;
uint8_t framebuffer[IMAGE_BUFFER_SIZE];
static uint32_t expected_jpeg_size = 0;
static uint32_t received_jpeg_size = 0;
static bool receiving_jpeg = false;
static volatile bool decode_success = false; // Flag để báo giải mã thành công

// Ring buffer functions
static void ring_buffer_write(RingBuffer_t *rb, const uint8_t *data, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
        if (rb->count < RING_BUFFER_SIZE) {
            rb->count++;
        } else {
            rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
        }
    }
}

static uint32_t ring_buffer_read(RingBuffer_t *rb, uint8_t *data, uint32_t length) {
    uint32_t bytes_to_read = (length > rb->count) ? rb->count : length;
    for (uint32_t i = 0; i < bytes_to_read; i++) {
        data[i] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
        rb->count--;
    }
    return bytes_to_read;
}

static uint32_t ring_buffer_available(RingBuffer_t *rb) {
    return rb->count;
}

// Callback nhận dữ liệu từ USB
void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event) {
    uint8_t tusb_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
    size_t rx_size = 0;

    esp_err_t ret = tinyusb_cdcacm_read(itf, tusb_buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret != ESP_OK || rx_size < 1) {
        // ESP_LOGW(TAG, "Read failed or too short: ret=%d, rx_size=%d", ret, rx_size);
        ESP_LOGW(TAG, "Read failed or too short" );
        return;
    }

    gpio_set_level(GPIO_NUM_6, 1);

    // Log dữ liệu thô để debug
    // ESP_LOGI(TAG, "Raw data: %02x %02x %02x %02x %02x %02x (first 6 bytes)", tusb_buf[0], tusb_buf[1], tusb_buf[2], tusb_buf[3], tusb_buf[4], tusb_buf[5]);

    // Kiểm tra Header
    if (!receiving_jpeg && rx_size >= 6 && tusb_buf[0] == 0xAA && tusb_buf[1] == 0xBB) {
        expected_jpeg_size = (tusb_buf[2] << 24) | (tusb_buf[3] << 16) | (tusb_buf[4] << 8) | tusb_buf[5];
        // ESP_LOGI(TAG, "Received Header, JPEG size: %" PRIu32 " bytes", expected_jpeg_size);
        receiving_jpeg = true;
        received_jpeg_size = 0;
        gpio_set_level(GPIO_NUM_10, 1);

        if (rx_size > 6) {
            ring_buffer_write(&ring_buffer, &tusb_buf[6], rx_size - 6);
            received_jpeg_size += rx_size - 6;
            // ESP_LOGI(TAG, "Received initial %d bytes, total: %" PRIu32 "/%" PRIu32,
            //          rx_size - 6, received_jpeg_size, expected_jpeg_size);
        }
    } else if (receiving_jpeg) {
        ring_buffer_write(&ring_buffer, tusb_buf, rx_size);
        received_jpeg_size += rx_size;
        // ESP_LOGI(TAG, "Received %d bytes, total: %" PRIu32 "/%" PRIu32,
        //          rx_size, received_jpeg_size, expected_jpeg_size);

        if (received_jpeg_size >= expected_jpeg_size) {
            ESP_LOGI(TAG, "Full JPEG received: %" PRIu32 " bytes", received_jpeg_size);
            receiving_jpeg = false;
            gpio_set_level(GPIO_NUM_10, 0);
        }
    } else {
        ESP_LOGW(TAG, "Unexpected data received, ignoring");
    }

    gpio_set_level(GPIO_NUM_6, 0);
}

// Task giải mã JPEG
void JPEG_decode_task(void *pvParameters) {
    ESP_LOGI(TAG, "JPEG_decode_task running on core %d", xPortGetCoreID());

    uint8_t *jpeg_buffer = malloc(RING_BUFFER_SIZE); // Buffer tạm để đọc từ ring buffer

    while (1) {
        if (!receiving_jpeg && ring_buffer_available(&ring_buffer) >= expected_jpeg_size && expected_jpeg_size != 0) {
            gpio_set_level(GPIO_NUM_9, 1);

            // Reset flag trước khi decode
            decode_success = false;

            // Đọc dữ liệu JPEG từ ring buffer
            uint32_t jpeg_size = ring_buffer_read(&ring_buffer, jpeg_buffer, expected_jpeg_size);

            // Cấu hình giải mã JPEG
            esp_jpeg_image_cfg_t jpeg_cfg = {
                .indata = jpeg_buffer,
                .indata_size = jpeg_size,
                .outbuf = framebuffer,
                .outbuf_size = IMAGE_BUFFER_SIZE,
                .out_format = JPEG_IMAGE_FORMAT_RGB565,
                .out_scale = JPEG_IMAGE_SCALE_0,
                .flags = {
                    .swap_color_bytes = 0, // Đảm bảo byte order đúng với ST7789
                }
            };

            // Giải mã JPEG
            esp_jpeg_image_output_t outimg;
            esp_err_t ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "JPEG decoded successfully! Size: %dpx x %dpx", outimg.width, outimg.height);
                decode_success = true; // Đặt flag khi giải mã thành công
            } else {
                ESP_LOGE(TAG, "JPEG prepare failed: ");
            }

            gpio_set_level(GPIO_NUM_9, 0);
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
    free(jpeg_buffer); // Không bao giờ đến đây vì vòng lặp vô hạn
}

// Task hiển thị lên ST7789
void ST7789_display_task(void *pvParameters) {
    ESP_LOGI(TAG, "ST7789_display_task running on core %d", xPortGetCoreID());

    while (1) {
        if (decode_success) { // Chỉ vẽ khi giải mã thành công
            gpio_set_level(GPIO_NUM_7, 1);
            for (int row = 0; row < SCREEN_HEIGHT; row++) {
                lcdDrawMultiPixels(&dev, 0, row, SCREEN_WIDTH, &framebuffer[row * SCREEN_WIDTH * 2]);
            }
            gpio_set_level(GPIO_NUM_7, 0);
            ESP_LOGI(TAG, "Displayed frame on ST7789");
            decode_success = false; // Reset flag sau khi vẽ
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void app_main(void) {
    
	//////////////////////////////////////////
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = NULL,
        .external_phy = false,
        .configuration_descriptor = NULL,
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 512,
        .callback_rx = &tinyusb_cdc_rx_callback, // the first way to register a callback
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };

    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");
	///////////////////////////////////////////////////////
	gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GPIO_NUM_6) | (1ULL << GPIO_NUM_7) | (1ULL << GPIO_NUM_9) | (1ULL << GPIO_NUM_10),
        .pull_up_en = 0,
        .pull_down_en = 0
    };
    gpio_config(&io_conf);
	//////////////////////////////
	spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO, CONFIG_BL_GPIO);
	lcdInit(&dev, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETX, CONFIG_OFFSETY);

	lcdFillScreen(&dev, RED);
	//////////////////////////////

    xTaskCreate(JPEG_decode_task, "JPEG_decode_task", 8192, NULL, 4, NULL);
    xTaskCreate(ST7789_display_task, "ST7789_display_task", 4096, NULL, 3, NULL);
}