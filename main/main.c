// Libraries
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"

// Definitions
#define LED_GPIO 2

// Function prototypes
static bool twai_rx_cb(twai_node_handle_t, const twai_rx_done_event_data_t*, void*);

// Variables
typedef struct {
    uint32_t id;
    uint8_t len;
    uint8_t data[8];
} CAN_msg_t;
static QueueHandle_t CAN_rx_queue;

// Main
void app_main(void) {

    // TWAI (Two-Wire Automotice Interface)
    // Setup
    twai_node_handle_t node_hdl = NULL;             // Create TWAI node
    twai_onchip_node_config_t node_config = {       // Configuration of TWAI node
    .io_cfg.tx = 4,                     // TWAI TX GPIO pin
    .io_cfg.rx = 5,                     // TWAI RX GPIO pin
    .bit_timing.bitrate = 200000,       // 200kbps bitrate
    .tx_queue_depth = 5,                // Transmit queue depth set to 5

    .flags.enable_self_test = true,     // Tx doesnt require ACK
    .flags.enable_loopback = true,      // Rx doesnt require ACK
    // .flags.enable_listen_only = true,   // Sets the TWAI node to listen-mode only
    };
    // Create a new TWAI controller driver instance
    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_hdl));

    // Rx Config 
    CAN_rx_queue = xQueueCreate(10, sizeof(CAN_msg_t));
    // Callback
    twai_event_callbacks_t user_cbs = {
        .on_rx_done = twai_rx_cb,
    };
    ESP_ERROR_CHECK(twai_node_register_event_callbacks(node_hdl, &user_cbs, CAN_rx_queue));

    // Start the TWAI controller
    ESP_ERROR_CHECK(twai_node_enable(node_hdl));

    // Tx Message Example
    uint8_t send_buff[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    twai_frame_t tx_msg = {
        .header.id = 0x1,                   // Message ID
        .header.ide = false,                // Use 29-bit extended ID format
        .buffer = send_buff,                // Pointer to data to transmit 
        .buffer_len = sizeof(send_buff),    // Length of data to transmit
    };
    ESP_ERROR_CHECK(twai_node_transmit(node_hdl, &tx_msg, 0));          // Timeout = 0: returns immediately if queue is full
    ESP_ERROR_CHECK(twai_node_transmit_wait_all_done(node_hdl, 1000));    // Wait for transmission to finish

    // Rx Message
    CAN_msg_t rx_msg;
    if (xQueueReceive(CAN_rx_queue, &rx_msg, pdMS_TO_TICKS(500)) == pdTRUE) {
        printf("RX id = 0x%03lX len=%d data: ", (unsigned long)rx_msg.id, rx_msg.len);
        for (int i = 0; i < rx_msg.len; i++) printf(" %02X", rx_msg.data[i]);
        printf("\n");
    } else {
        printf("No frame recieved\n");
    }

    // LED Output
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static bool twai_rx_cb(twai_node_handle_t handle, 
                        const twai_rx_done_event_data_t *edata, void *user_ctx)
{
    QueueHandle_t rx_queue = (QueueHandle_t)user_ctx;
    BaseType_t high_task_woken = pdFALSE;

    uint8_t recv_buff[8];
    twai_frame_t rx_frame = {
        .buffer = recv_buff,
        .buffer_len = sizeof(recv_buff),
    };

    if (twai_node_receive_from_isr(handle, &rx_frame) == ESP_OK) {
        // receive ok, do something here
        CAN_msg_t rx_msg = {
            .id = rx_frame.header.id,
            .len = rx_frame.header.dlc,
        };
        for (int i = 0; i < rx_msg.len && i < 8; i++) rx_msg.data[i] = rx_frame.buffer[i];
        xQueueSendFromISR(rx_queue, &rx_msg, &high_task_woken);
    }

    return (high_task_woken == pdTRUE);
}