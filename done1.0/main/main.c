#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "cJSON.h"
#include "driver/gpio.h"

#define UART_NUM UART_NUM_1
#define UART_TX_PIN 17
#define UART_RX_PIN 16
#define BUF_SIZE (1024)
#define LED_PIN 5
#define RESET_NOTIFY_NUMBER "+919409277556"
static const char *TAG = "SIM800L";
//static const char *TAG1 = "JSON";



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

/*void send_at_command(const char* cmd, const char* expected_response, int timeout_ms) {
    assert(cmd != NULL);
    assert(expected_response != NULL);

    uart_write_bytes(EX_UART_NUM, cmd, strlen(cmd));
    vTaskDelay(pdMS_TO_TICKS(100));  // Short delay to ensure command is sent

    uint8_t data[BUF_SIZE] = {0};
    int length = uart_read_bytes(EX_UART_NUM, data, BUF_SIZE, pdMS_TO_TICKS(timeout_ms));
    if (length >= 0) {
        data[length] = '\0';
        ESP_LOGI(TAG, "Response: %s", data);

        if (strstr((const char*)data, expected_response) == NULL) {
            ESP_LOGE(TAG, "Unexpected response for command: %s", cmd);
        }
    } else {
        ESP_LOGE(TAG, "Error reading response for command: %s", cmd);
    }
}*/

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
    //uart_send("AT+CMGF=1");
    //uart_receive(buf, BUF_SIZE);
    //uart_send("AT+CNMI=2,2,0,0,0");
    //uart_receive(buf, BUF_SIZE);
    uart_send("AT+CIMI");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+CGSN");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+GSN");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=3,1,\"APN\",\"airtelgprs.com\""); // Replace 'your_apn' with your SIM's APN
    uart_receive(buf, BUF_SIZE);
    //uart_send("AT+SAPBR=2,1");
    //uart_receive(buf, BUF_SIZE);
   // uart_send("AT+SAPBR=2,1");
    //uart_receive(buf, BUF_SIZE);
    //if (strstr(buf, "OK")) {
    //    gpio_set_level(LED_PIN, 1);  // Set LED_PIN high
    //} else {
    //   gpio_set_level(LED_PIN, 0);  // Set LED_PIN low
    //}
}
void connect_gprs1() {
	char buf[BUF_SIZE];
	uart_send("AT+SAPBR=1,1");
	uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=2,1");
    uart_receive(buf, BUF_SIZE);
    if (strstr(buf, "OK")) {
        gpio_set_level(LED_PIN, 1);  // Set LED_PIN high
    } else {
        gpio_set_level(LED_PIN, 0);  // Set LED_PIN low
    }
  }
void http_post_request(const char* url, const char *post_data) {
    char buf[BUF_SIZE];
    //uart_send("AT+SAPBR=2,1");
    //uart_receive(buf, BUF_SIZE);
    connect_gprs1();
    uart_send("AT+HTTPINIT");
    uart_receive(buf, BUF_SIZE);
    // uart_send("AT+HTTPSSL=1");
    //uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPPARA=\"CID\",1");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPPARA=\"URL\",\"http://defx-pos-dev.herokuapp.com/one_phones\"");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
    uart_receive(buf, BUF_SIZE);
    sprintf(buf, "AT+HTTPDATA=%d,10000", strlen(post_data));
    uart_send(buf);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    uart_send(post_data);
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPACTION=1");
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for the response
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPTERM");
    uart_receive(buf, BUF_SIZE);
    }

void configure_sms() {
	char buf[BUF_SIZE];
    uart_send("AT+CMGF=1");  // Set SMS text mode
    uart_send("AT+CNMI=2,2,0,0,0");  // Configure SMS notifications
    uart_send("AT+SAPBR=2,1");
    uart_receive(buf, BUF_SIZE);
    if (strstr(buf, "OK")) {
        gpio_set_level(LED_PIN, 1);  // Set LED_PIN high
    } else {
        gpio_set_level(LED_PIN, 0);  // Set LED_PIN low
    }
}
void send_sms(const char* phone_number, const char* message) {
    char buf[BUF_SIZE];
    uart_send("AT+CMGF=1");  // Set SMS to text mode
    uart_receive(buf, BUF_SIZE);
    sprintf(buf, "AT+CMGS=\"%s\"", phone_number);
    uart_send(buf);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    uart_send(message);
    uart_write_bytes(UART_NUM, "\x1A", 1);  // Send Ctrl+Z to end the message
    uart_receive(buf, BUF_SIZE);
    ESP_LOGI(TAG, "Sent SMS to %s: %s", phone_number, message);
}
void sms_task(void *pvParameters) {
    uint8_t data[BUF_SIZE] = {0};
    while (1) {
        int length = uart_read_bytes(UART_NUM, data, BUF_SIZE, pdMS_TO_TICKS(1000));
        if (length > 0) {
            data[length] = '\0';
            ESP_LOGI(TAG, "Incoming data: %s", data);

            if (strstr((const char*)data, "+CMT:")) {
                ESP_LOGI(TAG, "SMS Received: %s", data);

                // Extract phone number and SMS content
                char* phone_start = strchr((char*)data, '\"');
                if (phone_start) {
                    phone_start++;
                    char* phone_end = strchr(phone_start, '\"');
                    if (phone_end) {
                        *phone_end = '\0';

                        char* sms_start = strstr(phone_end + 1, "\r\n");
                        if (sms_start) {
                            sms_start += 2;
                            char* sms_end = strstr(sms_start, "\r\n");
                            if (sms_end) {
                                *sms_end = '\0';
                                 if (strcmp(sms_start, "RESET") == 0) {
                                    ESP_LOGI(TAG, "Reset command received, sending notification and restarting...");
                                    send_sms(RESET_NOTIFY_NUMBER, "ESP32 is resetting...");  // Send SMS notification
                                    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Short delay before reset
                                    esp_restart();
            }

                                ESP_LOGI(TAG, "Extracted Phone: %s", phone_start);
                                ESP_LOGI(TAG, "Extracted SMS: %s", sms_start);

                                 // Prepare the POST data
                                cJSON *root = cJSON_CreateObject();
                                cJSON_AddStringToObject(root, "phone_from", "8849384442");
                                cJSON_AddStringToObject(root, "phone_to", phone_start);
                                cJSON_AddStringToObject(root, "message", sms_start);
    
                                char *post_data = cJSON_Print(root);
                                // Send POST request
                                http_post_request("http://defx-pos-dev.herokuapp.com/one_phones", post_data);
                            }
                        }
                    }
                }
            }
        }
    }
}


void app_main() {
    uart_init();
    ESP_LOGI(TAG, "Starting up");
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    connect_gprs();
    configure_sms();  // Configure SMS settings
    xTaskCreate(sms_task, "sms_task", 8192, NULL, 5, NULL);  // Increased stack size to 8192

    ESP_LOGI(TAG, "Operation completed");
    
    //cJSON_Delete(root);
   // free(post_data);
}
