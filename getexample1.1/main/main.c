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
#include "mbedtls/base64.h"

#define UART_NUM UART_NUM_1
#define TXD_PIN 17
#define RXD_PIN 16
#define BUF_SIZE 1024




void init_uart() {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM, 1024 * 2, 0, 0, NULL, 0);
}
void sendCommand(const char* cmd, unsigned long waitTime) {
    uart_write_bytes(UART_NUM, cmd, strlen(cmd));
    uart_write_bytes(UART_NUM, "\r\n", 2);  // Send carriage return and newline
    vTaskDelay(waitTime / portTICK_PERIOD_MS);

    uint8_t data[1024];
    int len = uart_read_bytes(UART_NUM, data, 1024, waitTime / portTICK_PERIOD_MS);
    if (len > 0) {
        data[len] = 0;  // Null-terminate the received data
        printf("%s\n", data);  // Print received data
    }
}


void send_basic_auth_request() {
    const char *username = "865210034872062";
    const char *password = "866782047458043";

    // Combine and encode username:password
    char auth_string[128];
    snprintf(auth_string, sizeof(auth_string), "%s:%s", username, password);

    unsigned char encoded_data[128];
    size_t encoded_len;
    mbedtls_base64_encode(encoded_data, sizeof(encoded_data), &encoded_len, 
                          (unsigned char *)auth_string, strlen(auth_string));
    encoded_data[encoded_len] = '\0';

    // Create the Authorization header
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Basic %s", encoded_data);

    // Send the HTTP request
    sendCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", 1000);
    sendCommand("AT+SAPBR=3,1,\"APN\",\"airtelgprs.com\"", 1000);
    sendCommand("AT+SAPBR=1,1", 2000);
    sendCommand("AT+HTTPINIT", 1000);
    sendCommand("AT+HTTPPARA=\"CID\",1", 1000);
    sendCommand("AT+HTTPPARA=\"URL\",\"http://one-phone-backend-6adbf4e679b0.herokuapp.com/api/devices\"", 1000);

    // Set the Authorization header
    char at_command[512];
    snprintf(at_command, sizeof(at_command), "AT+HTTPPARA=\"USERDATA\",\"%s\"", auth_header);
    sendCommand(at_command, 1000);

    // Perform the GET request
    sendCommand("AT+HTTPACTION=0", 1000);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    sendCommand("AT+HTTPREAD", 1000);

    // Terminate the session
    sendCommand("AT+HTTPTERM", 1000);
   
}

void app_main() {
    init_uart();  // Initialize UART
    send_basic_auth_request();  // Send the HTTP request
}
