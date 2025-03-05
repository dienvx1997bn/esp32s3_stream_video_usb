#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "st7789.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "jpeg_decoder.h"

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240
#define IMAGE_BUFFER_SIZE  (SCREEN_WIDTH * SCREEN_HEIGHT * 2)
#define JPEG_QUEUE_SIZE    5  // Tối đa 5 JPEG buffer
#define JPEG_MAX_SIZE      (32 * 1024)

#define CONFIG_MOSI_GPIO   16
#define CONFIG_SCLK_GPIO   15
#define CONFIG_CS_GPIO     -1
#define CONFIG_DC_GPIO     18
#define CONFIG_RESET_GPIO  17
#define CONFIG_BL_GPIO     8

static const char *TAG = "JPEG_OPT";

TFT_t dev;
uint8_t current_buffer[IMAGE_BUFFER_SIZE];
uint8_t new_buffer[IMAGE_BUFFER_SIZE];

typedef struct {
    uint8_t data[JPEG_MAX_SIZE];
    uint32_t size;
} jpeg_buffer_t;

// Hàng đợi cho các buffer JPEG
static QueueHandle_t jpeg_queue;
static bool receiving_jpeg = false;
static uint32_t expected_jpeg_size = 0;

// Gửi tín hiệu "ready" về PC
void send_ready_signal() {
    const char *ready_msg = "ready\n";
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)ready_msg, strlen(ready_msg));
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
}

// Callback nhận dữ liệu từ USB
void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event) {
    static jpeg_buffer_t *jpeg_buf = NULL;

    gpio_set_level(GPIO_NUM_6, 1);
    if (!jpeg_buf) {
        jpeg_buf = malloc(sizeof(jpeg_buffer_t));
        jpeg_buf->size = 0;
    }

    uint8_t usb_data[512];
    size_t rx_size = 0;
    esp_err_t ret = tinyusb_cdcacm_read(itf, usb_data, sizeof(usb_data), &rx_size);

    if (ret != ESP_OK || rx_size == 0) return;

    if (!receiving_jpeg && rx_size >= 6 && usb_data[0] == 0xAA && usb_data[1] == 0xBB) {
        expected_jpeg_size = (usb_data[2] << 24) | (usb_data[3] << 16) | (usb_data[4] << 8) | usb_data[5];
        receiving_jpeg = true;
        jpeg_buf->size = 0;
        gpio_set_level(GPIO_NUM_10, 1);

        if (rx_size > 6) {
            memcpy(jpeg_buf->data, &usb_data[6], rx_size - 6);
            jpeg_buf->size += rx_size - 6;
        }
    } else if (receiving_jpeg) {
        memcpy(jpeg_buf->data + jpeg_buf->size, usb_data, rx_size);
        jpeg_buf->size += rx_size;

        if (jpeg_buf->size >= expected_jpeg_size) {
            receiving_jpeg = false;
            xQueueSend(jpeg_queue, &jpeg_buf, portMAX_DELAY);
            jpeg_buf = NULL;
            gpio_set_level(GPIO_NUM_10, 0);
        }
    }
    gpio_set_level(GPIO_NUM_6, 0);
}

// Task giải mã JPEG
void JPEG_decode_task(void *pvParameters) {
    while (1) {
        jpeg_buffer_t *jpeg_buf = NULL;

        if(uxQueueSpacesAvailable(jpeg_queue) > 0) {
            send_ready_signal();
        }

        if (xQueueReceive(jpeg_queue, &jpeg_buf, portMAX_DELAY) == pdTRUE) {
            gpio_set_level(GPIO_NUM_9, 1);
            esp_jpeg_image_cfg_t jpeg_cfg = {
                .indata = jpeg_buf->data,
                .indata_size = jpeg_buf->size,
                .outbuf = new_buffer,
                .outbuf_size = IMAGE_BUFFER_SIZE,
                .out_format = JPEG_IMAGE_FORMAT_RGB565,
                .out_scale = JPEG_IMAGE_SCALE_0,
                .flags = {.swap_color_bytes = 1},
            };

            esp_jpeg_image_output_t outimg;
            if (esp_jpeg_decode(&jpeg_cfg, &outimg) == ESP_OK) {
                // send_ready_signal();
            }

            free(jpeg_buf); // Giải phóng bộ nhớ
            gpio_set_level(GPIO_NUM_9, 0);
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

// Task hiển thị ST7789 với so sánh từng dòng
void ST7789_display_task(void *pvParameters) {
    while (1) {
        gpio_set_level(GPIO_NUM_7, 1);
        for (int row = 0; row < SCREEN_HEIGHT; row++) {
            uint16_t *new_row = (uint16_t *)&new_buffer[row * SCREEN_WIDTH * 2];
            uint16_t *current_row = (uint16_t *)&current_buffer[row * SCREEN_WIDTH * 2];

            if (memcmp(new_row, current_row, SCREEN_WIDTH * 2) != 0) {
                lcdDrawMultiPixels_2(&dev, 0, row, SCREEN_WIDTH, new_row);
                memcpy(current_row, new_row, SCREEN_WIDTH * 2);
            }
        }
        gpio_set_level(GPIO_NUM_7, 0);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void app_main(void) {
    memset(current_buffer, 0x00, IMAGE_BUFFER_SIZE);
    memset(new_buffer, 0x00, IMAGE_BUFFER_SIZE);

    jpeg_queue = xQueueCreate(JPEG_QUEUE_SIZE, sizeof(jpeg_buffer_t *));

    const tinyusb_config_t tusb_cfg = {
        .external_phy = false,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 512,
        .callback_rx = &tinyusb_cdc_rx_callback,
    };
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));

    send_ready_signal();

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GPIO_NUM_6) | (1ULL << GPIO_NUM_7) | (1ULL << GPIO_NUM_9) | (1ULL << GPIO_NUM_10),
        .pull_up_en = 0,
        .pull_down_en = 0
    };
    gpio_config(&io_conf);

    spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO, CONFIG_BL_GPIO);
    lcdInit(&dev, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0);
    lcdFillScreen(&dev, RED);

    xTaskCreatePinnedToCore(JPEG_decode_task, "JPEG_decode", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(ST7789_display_task, "ST7789_display", 4096, NULL, 4, NULL, 0);
}