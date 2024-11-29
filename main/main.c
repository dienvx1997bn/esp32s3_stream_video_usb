#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "ESP_LOG.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"

#include "st7789.h"
#include "fontx.h"
#include "bmpfile.h"
#include "decode_jpeg.h"


#include <stdint.h>
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "sdkconfig.h"

#include "freertos/semphr.h"
#include "esp_async_memcpy.h"
#include "esp_heap_caps.h"
#include "esp_async_memcpy.h"

#include "driver/gpio.h"


// #ifndef CONFIG_TINYUSB_CDC_RX_BUFSIZE
// #define CONFIG_TINYUSB_CDC_RX_BUFSIZE 512
// #endif
// #define CONFIG_WIDTH  240
// #define CONFIG_HEIGHT 240
// #define CONFIG_MOSI_GPIO 16
// #define CONFIG_SCLK_GPIO 15
// #define CONFIG_CS_GPIO -1
// #define CONFIG_DC_GPIO 18
// #define CONFIG_RESET_GPIO 17
// #define CONFIG_BL_GPIO 8
#define DATA_QUEUE_SIZE		7

static const char *TAG = "TEST";
const char *file_names[] = {	"/spiffs/image0.jpeg", 
							"/spiffs/image1.jpeg", 
							"/spiffs/image2.jpeg", 
							"/spiffs/image3.jpeg", 
							"/spiffs/image4.jpeg",
							"/spiffs/image5.jpeg",
							"/spiffs/image6.jpeg",
							"/spiffs/image7.jpeg",
							"/spiffs/image8.jpeg", 
							"/spiffs/image9.jpeg", 
						 };

uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1] = {0};
 uint8_t image_index_processed = 0;
 bool data_ready_to_draw = false;
 bool send_response = false;
 bool new_image_received = false;

SemaphoreHandle_t my_semaphore;

uint16_t colors[sizeof(uint16_t) * CONFIG_WIDTH] = {0};
uint16_t number_of_image_received = 0;
uint16_t number_of_image_in_queue = 0;
uint16_t store_image_buffer_size = 0;

typedef struct {
	char file_name[20];
	uint16_t buffer_size;
	uint8_t store_image_buffer[12 * 1024];
} Queue_Data;

Queue_Data queue_data_recv_usb;
Queue_Data queue_data_store;

typedef struct {
	pixel_jpeg **pixels;
} Queue_Image;

QueueHandle_t bufferQueueRecvData;
QueueHandle_t bufferQueueRecvData;


////////////////////////////////////////////////

void ST7789_Draw(void *pvParameters)
{
	// TickType_t startTick, endTick, diffTick;
	TFT_t dev;
	spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO, CONFIG_BL_GPIO);
	lcdInit(&dev, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETX, CONFIG_OFFSETY);

	lcdFillScreen(&dev, RED);

	//ESP_LOGI(TAG, "ST7789_Draw is running on core %d",xPortGetCoreID()); 
	while(true) {

		if(data_ready_to_draw == true)
		{
			// startTick = xTaskGetTickCount();
			gpio_set_level(GPIO_NUM_46, 1);
			ESP_LOGI(__FUNCTION__, "start draw %d %d", number_of_image_in_queue, number_of_image_received);


			uint16_t _width = CONFIG_WIDTH;
			uint16_t _cols = 0;
			uint16_t y = 0;
			xSemaphoreTake(my_semaphore,100);
			for(y = 0; y < CONFIG_HEIGHT; y++){
				gpio_set_level(GPIO_NUM_46, 1);
				// for(int x = 0;x < _width; x++){
				// 	//pixel_jpeg pixel = pixels[y][x];
				// 	//colors[x] = rgb565_conv(pixel.red, pixel.green, pixel.blue);
				// 	colors[x] = pixels[y][x];
				// }
				lcdDrawMultiPixels(&dev, _cols, y, _width, &pixels[y][0]);
				// vTaskDelay(1);
				gpio_set_level(GPIO_NUM_46, 0);
			}
			
			// Clear buffer
			release_image(&pixels, CONFIG_WIDTH, CONFIG_HEIGHT);
			xSemaphoreGive(my_semaphore);
			data_ready_to_draw = false;

			// ESP_LOGI(__FUNCTION__, "end draw %d %d", number_of_image_in_queue, number_of_image_received);

			gpio_set_level(GPIO_NUM_46, 0);
			// endTick = xTaskGetTickCount();
			// diffTick = endTick - startTick;
			// //ESP_LOGI(__FUNCTION__, "elapsed time[ms]:%"PRIu32,diffTick*portTICK_PERIOD_MS);
		}
		
		vTaskDelay(1);
	} // end while
}

void ST7789_Decode(void *pvParameters)
{
	// TickType_t startTick, endTick, diffTick;
	int imageWidth;
	int imageHeight;
	
	//ESP_LOGI(TAG, "ST7789_Decode is running on core %d",xPortGetCoreID()); 
	
	// char file[32];
	// strcpy(file, "/spiffs/image.jpeg");

	while (1) {
		if(data_ready_to_draw == false && number_of_image_received > 0) {
			gpio_set_level(GPIO_NUM_10, 1);
			// startTick = xTaskGetTickCount();

			// If aready has image in flash then process
			xSemaphoreTake(my_semaphore,100);
			esp_err_t err = decode_jpeg(&pixels, file_names[image_index_processed], CONFIG_WIDTH, CONFIG_HEIGHT, &imageWidth, &imageHeight);
			xSemaphoreGive(my_semaphore);
			//ESP_LOGD(__FUNCTION__, "decode_image err=%d imageWidth=%d imageHeight=%d", err, imageWidth, imageHeight);
			image_index_processed = (image_index_processed + 1 ) % DATA_QUEUE_SIZE;
			ESP_LOGI(__FUNCTION__, "decode %d %d", number_of_image_in_queue, number_of_image_received);
			if (err == ESP_OK) {
				data_ready_to_draw = true;
			} else {
				ESP_LOGE(__FUNCTION__, "decode_jpeg fail=%d", err);
			}
			
			gpio_set_level(GPIO_NUM_10, 0);
			// endTick = xTaskGetTickCount();
			// diffTick = endTick - startTick;
			// //ESP_LOGI(__FUNCTION__, "elapsed time[ms]:%"PRIu32,diffTick*portTICK_PERIOD_MS);
		}
		vTaskDelay(1);
	}
}


void Store_image(void *pvParameters)
{
	
	while(1) {
		if (number_of_image_in_queue > 0
			&& xQueueReceive(bufferQueueRecvData, &queue_data_store, portMAX_DELAY) == pdTRUE)
		{
			gpio_set_level(GPIO_NUM_9, 1);
			// Use POSIX and C standard library functions to work with files.
			// First create a file.
			// //ESP_LOGI(TAG, "Opening file");
			FILE* f = fopen(queue_data_store.file_name, "w");
			// if (f == NULL) {
			// 	//ESP_LOGE(TAG, "Failed to open file for writing");
			// 	return;
			// }
			fwrite(queue_data_store.store_image_buffer, queue_data_store.buffer_size, 1, f);
			fclose(f);
			number_of_image_in_queue -= 1;
			// //ESP_LOGI(TAG, "File written");
			ESP_LOGI(__FUNCTION__, "image stored %d %d", number_of_image_in_queue, number_of_image_received);
			gpio_set_level(GPIO_NUM_9, 0);
		}
		vTaskDelay(5);
	}
}


void USB_response(void *pvParameters)
{
	char msg[10] = {0};
	strcpy(msg, "accepted\n");

	while(1) {
		// if new data received then send to queue
		if (new_image_received == true) {
			gpio_set_level(GPIO_NUM_3, 1);
			// //ESP_LOGI(TAG, "New image receive");
			strcpy(queue_data_recv_usb.file_name, file_names[number_of_image_received % DATA_QUEUE_SIZE]);
			queue_data_recv_usb.buffer_size = store_image_buffer_size;
			// gpio_set_level(GPIO_NUM_3, 0);
			if (xQueueSend(bufferQueueRecvData, &queue_data_recv_usb , (TickType_t)0 ) == pdTRUE) {
				ESP_LOGI(__FUNCTION__, "Item successfully added to the queue. %s", queue_data_recv_usb.file_name);
			} else {
				ESP_LOGE(__FUNCTION__, "Queue is full, failed to add item.");
			}
			
			store_image_buffer_size = 0;
			new_image_received = false;
			number_of_image_received += 1;
			// memset(&queue_data_recv_usb, 0, sizeof(Queue_Data));
			// send_response = true;
			number_of_image_in_queue += 1;
			gpio_set_level(GPIO_NUM_3, 0);
		}
		// When the file is transfer success or stored in spiffs then it is ready to get new data
		if (uxQueueSpacesAvailable(bufferQueueRecvData) > 0 
			&& number_of_image_received > 0
			// && number_of_image_in_queue < DATA_QUEUE_SIZE
			// && number_of_image_in_queue > 0
			// && send_response == true
			&& (number_of_image_in_queue < DATA_QUEUE_SIZE)
			) {
			// send_response = false;
			/* write back */
			ESP_LOGI(__FUNCTION__, "freespace %d write back %d %d", uxQueueSpacesAvailable(bufferQueueRecvData), number_of_image_in_queue, number_of_image_received);
			tinyusb_cdcacm_write_flush(0, 0);
			tinyusb_cdcacm_write_queue(0, (const uint8_t *)msg, 8);
			tinyusb_cdcacm_write_flush(0, 0);
		} else {
			// ESP_LOGI(__FUNCTION__, "not send accepted");
		}
		
		vTaskDelay(20);
	}
}


////////////////////////////////////////////////
// Create a semaphore used to report the completion of async memcpy
// SemaphoreHandle_t my_semaphore;
// async_memcpy_handle_t driver_handle = NULL;

// // Callback implementation, running in ISR context
// static bool my_async_memcpy_cb(async_memcpy_handle_t mcp_hdl, async_memcpy_event_t *event, void *cb_args)
// {
//     SemaphoreHandle_t sem = (SemaphoreHandle_t)cb_args;
//     BaseType_t high_task_wakeup = pdFALSE;
//     xSemaphoreGiveFromISR(my_semaphore, &high_task_wakeup); // high_task_wakeup set to pdTRUE if some high priority task unblocked
//     return high_task_wakeup == pdTRUE;
// }

////////////////////////////////////////////////
void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    /* initialization */
    size_t rx_size = 0;
	// static uint16_t index_buffer = 0;
	static uint16_t length_to_copy = 0;
	

    /* read */
	// gpio_set_level(GPIO_NUM_3, 0);
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {
		// index_buffer = buf[0] | buf[1] << 8;
		length_to_copy = buf[2] | buf[3] << 8;
		// //ESP_LOGI(TAG, "index_buffer %d length_to_copy %d rx_size %d byte_copied %d", index_buffer, length_to_copy, rx_size, store_image_buffer_size);
		if (length_to_copy == 0) {
			new_image_received = true;
		}
		else {
			memcpy(&(queue_data_recv_usb.store_image_buffer[store_image_buffer_size]), &buf[4], length_to_copy);
			store_image_buffer_size += length_to_copy;
		}
    } else {
        //ESP_LOGE(TAG, "Read error");
    }
	// gpio_set_level(GPIO_NUM_3, 1);
}

// void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
// {
//     int dtr = event->line_state_changed_data.dtr;
//     int rts = event->line_state_changed_data.rts;
//     //ESP_LOGI(TAG, "Line state changed on channel %d: DTR:%d, RTS:%d", itf, dtr, rts);
// }



////////////////////////////////////////////////
void app_main(void)
{
	esp_vfs_spiffs_conf_t conf = {
		.base_path = "/spiffs",
		.partition_label = NULL,
		.max_files = 12,
		.format_if_mount_failed =true
	};

	// Use settings defined above toinitialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is anall-in-one convenience function.
	esp_err_t ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			//ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			//ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			//ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",esp_err_to_name(ret));
		}
		return;
	}

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(NULL, &total,&used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG,"Failed to get SPIFFS partition information (%s)",esp_err_to_name(ret));
	} else {
		ESP_LOGI(TAG,"Partition size: total: %d, used: %d", total, used);
	}

	///////////////////////////////////////////////////////


	//ESP_LOGI(TAG, "USB initialization");
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
    // /* the second way to register a callback */
    // ESP_ERROR_CHECK(tinyusb_cdcacm_register_callback(
    //                     TINYUSB_CDC_ACM_0,
    //                     CDC_EVENT_LINE_STATE_CHANGED,
    //                     &tinyusb_cdc_line_state_changed_callback));

#if (CONFIG_TINYUSB_CDC_COUNT > 1)
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_1;
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
    ESP_ERROR_CHECK(tinyusb_cdcacm_register_callback(
                        TINYUSB_CDC_ACM_1,
                        CDC_EVENT_LINE_STATE_CHANGED,
                        &tinyusb_cdc_line_state_changed_callback));
#endif

//     //ESP_LOGI(TAG, "USB initialization DONE");
	///////////////////////////////////////////////////////
	gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << GPIO_NUM_3) | (1ULL << GPIO_NUM_46) | (1ULL << GPIO_NUM_9) | (1ULL << GPIO_NUM_10),
        .pull_up_en = 0,
        .pull_down_en = 0
    };
    gpio_config(&io_conf);
	//////////////////////////////
	// async_memcpy_config_t config = ASYNC_MEMCPY_DEFAULT_CONFIG();
	// // update the maximum data stream supported by underlying DMA engine
	// config.backlog = 8;
	my_semaphore = xSemaphoreCreateBinary();
	// ESP_ERROR_CHECK(esp_async_memcpy_install_gdma_ahb(&config, &driver_handle)); // install driver with default DMA engine
	//////////////////////////////

	bufferQueueRecvData = xQueueCreate(DATA_QUEUE_SIZE + 1, sizeof(Queue_Data));
	// bufferQueueDecodedData = xQueueCreate(DATA_QUEUE_SIZE, sizeof(Queue_Data));

	xTaskCreatePinnedToCore(ST7789_Draw, "ST7789_Draw", 1024 * 8, NULL, 4, NULL, 1);
	xTaskCreatePinnedToCore(ST7789_Decode, "ST7789_Decode", 1024 * 6, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(Store_image, "Store_image", 1024 * 6, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(USB_response, "USB_response", 1024 * 6, NULL, 5, NULL, 0);
	
}
