#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "cJSON.h"

#define UART_NUM UART_NUM_1
#define UART_TX_PIN 17
#define UART_RX_PIN 16
#define BUF_SIZE (1024)
#define SMS_QUEUE_SIZE 10

static const char *TAG = "SIM800L";

static QueueHandle_t sms_queue;

void uart_init() {
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void uart_send(const char *data) {
    uart_write_bytes(UART_NUM, data, strlen(data));
    uart_write_bytes(UART_NUM, "\r\n", 2);
}

void uart_receive(char *buf, int buf_len) {
    int len = uart_read_bytes(UART_NUM, (uint8_t *)buf, buf_len - 1, 1000 / portTICK_PERIOD_MS);
    if (len > 0) {
        buf[len] = 0;
        ESP_LOGI(TAG, "Received: %s", buf);
    }
}

void connect_gprs() {
    char buf[BUF_SIZE];
    uart_send("AT");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+CMGF=1");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+CNMI=1,2,0,0,0");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=3,1,\"APN\",\"airtelgprs.com\""); // Replace 'your_apn' with your SIM's APN
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=1,1");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=2,1");
    uart_receive(buf, BUF_SIZE);
    //uart_send("AT+HTTPPARA=\"PROTOCOL\",1"); // 1 for HTTPS
    //uart_receive(buf, BUF_SIZE);
    //uart_send("AT+HTTPSSL=1");
    //uart_receive(buf, BUF_SIZE);
}

void http_post_request(const char* url, const char *post_data) {
    char buf[BUF_SIZE];
    
    // Initialize HTTP
    uart_send("AT+HTTPINIT");
    uart_receive(buf, BUF_SIZE);
    
    // Set the bearer profile and GPRS connection
    uart_send("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=3,1,\"APN\",\"airtelgprs.com\""); // Replace with your APN
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=2,1");
    uart_receive(buf, BUF_SIZE);
    
    // Set the connection ID
    uart_send("AT+HTTPPARA=\"CID\",1");
    uart_receive(buf, BUF_SIZE);
    
    // Set the protocol to HTTPS
    //uart_send("AT+HTTPPARA=\"PROTOCOL\",1"); // 1 for HTTPS
    //uart_receive(buf, BUF_SIZE);
    
    //uart_send("AT+HTTPSSL=1");
    //uart_receive(buf, BUF_SIZE);
    
    // Set the URL
    sprintf(buf, "AT+HTTPPARA=\"URL\",\"%s\"", url);
    uart_send(buf);
    uart_receive(buf, BUF_SIZE);
    
   
    // Set the content type (application/json)
    uart_send("AT+HTTPPARA=\"CONTENT\",\"application/vnd.one-phone.v1\"");
    uart_receive(buf, BUF_SIZE);
    
    // Set the POST data length
    sprintf(buf, "AT+HTTPDATA=%d,10000", strlen(post_data));
    uart_send(buf);
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait for the module to be ready for data
    uart_send(post_data);
    uart_receive(buf, BUF_SIZE);
    
    // Perform the POST action
    uart_send("AT+HTTPACTION=1"); // 1 for POST
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for the response
    
    // Read the HTTP response
    uart_receive(buf, BUF_SIZE);
    
    // Terminate the HTTP session
    uart_send("AT+HTTPTERM");
    uart_receive(buf, BUF_SIZE);
}


void configure_sms() {
    uart_send("AT+CMGF=1");  // Set SMS text mode
    uart_send("AT+CNMI=2,2,0,0,0");  // Configure SMS notifications
}

void sms_task(void *pvParameters) {
    uint8_t data[BUF_SIZE] = {0};
    while (1) {
        int length = uart_read_bytes(UART_NUM, data, BUF_SIZE, pdMS_TO_TICKS(1000));
        if (length > 0) {
            data[length] = '\0';
            ESP_LOGI(TAG, "Incoming data: %s", data);

            if (strstr((const char*)data, "+CMT:")) {
                char phone[BUF_SIZE];
                char sms[BUF_SIZE];
                // Extract phone number and SMS content
                char* phone_start = strchr((char*)data, '\"');
                if (phone_start) {
                    phone_start++;
                    char* phone_end = strchr(phone_start, '\"');
                    if (phone_end) {
                        *phone_end = '\0';
                        strcpy(phone, phone_start);

                        char* sms_start = strstr(phone_end + 1, "\r\n");
                        if (sms_start) {
                            sms_start += 2;
                            char* sms_end = strstr(sms_start, "\r\n");
                            if (sms_end) {
                                *sms_end = '\0';
                                strcpy(sms, sms_start);
                                
                                   ESP_LOGI(TAG, "Extracted Phone: %s", phone_start);
                                   ESP_LOGI(TAG, "Extracted SMS: %s", sms_start);

                                // Send the phone and SMS to the queue
                                cJSON *root = cJSON_CreateObject();
                                cJSON_AddStringToObject(root, "phone_from", "8849384442");
                                cJSON_AddStringToObject(root, "phone_to", phone);
                                cJSON_AddStringToObject(root, "message", sms);
                                char *post_data = cJSON_Print(root);

                                // Send the POST request to the queue
                                xQueueSend(sms_queue, &post_data, portMAX_DELAY);

                                //cJSON_Delete(sms_data);
                               // free(post_data);
                            }
                        }
                    }
                }
            }
        }
    }
}

void http_task(void *pvParameters) {
    char *post_data;
    while (1) {
        if (xQueueReceive(sms_queue, &post_data, portMAX_DELAY)) {
            http_post_request("http://defx-pos-dev.herokuapp.com/one_phones", post_data);
            free(post_data);  // Free the POST data after sending
        }
    }
}

void app_main() {
    uart_init();
    ESP_LOGI(TAG, "Starting up");

    sms_queue = xQueueCreate(SMS_QUEUE_SIZE, sizeof(char *));
    if (sms_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create SMS queue");
        return;
    }

    connect_gprs();
    configure_sms();  // Configure SMS settings
    xTaskCreate(sms_task, "sms_task", 8192, NULL, 10, NULL);  // Increased stack size to 8192
    xTaskCreate(http_task, "http_task", 8192, NULL, 5, NULL);  // Task to handle HTTP POST requests

    ESP_LOGI(TAG, "Operation completed");
}
