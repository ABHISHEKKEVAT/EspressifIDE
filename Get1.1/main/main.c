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
#include "driver/gpio.h"
#include "cJSON.h"

#define UART_NUM UART_NUM_1
#define UART_TX_PIN 17
#define UART_RX_PIN 16
#define BUF_SIZE (1024)
#define APN "airtelgprs.com"  // Replace with your SIM's APN
#define HTTP_URL "http://defx-pos-dev.herokuapp.com/one_phones"
#define PHONE_FROM "phone_from_data"
#define LED_PIN 2

static const char *TAG = "SIM800L";
static char phone_from_data[50];  // Buffer to store the phone_from data

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
    uart_send("AT+SAPBR=3,1,\"APN\",\"" APN "\"");
    uart_receive(buf, BUF_SIZE);
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
    uart_send("AT+HTTPINIT");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPPARA=\"CID\",1");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPPARA=\"URL\",\"" HTTP_URL "\"");
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

void http_get_request(const char* url) {
    char buf[BUF_SIZE];
    char http_response[BUF_SIZE * 2];  // Buffer to store the HTTP response
    char* json_start = NULL;           // Pointer to the start of the JSON data

    uart_send("AT+HTTPINIT");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPPARA=\"CID\",1");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPPARA=\"URL\",\"" HTTP_URL "\"");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPACTION=0");  // 0 for GET method
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for the response
    uart_receive(buf, BUF_SIZE);

    // Read the HTTP response
    uart_send("AT+HTTPREAD");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    int len = uart_read_bytes(UART_NUM, (uint8_t *)http_response, sizeof(http_response) - 1, 1000 / portTICK_PERIOD_MS);
    if (len > 0) {
        http_response[len] = '\0';  // Null terminate the buffer
        ESP_LOGI(TAG, "HTTP GET Response: %s", http_response);
    } else {
        ESP_LOGE(TAG, "Failed to read HTTP response");
        return;
    }

    // Find the start of the JSON data
    json_start = strchr(http_response, '[{');
    if (!json_start) {
        ESP_LOGE(TAG, "Failed to find the start of JSON data");
        return;
    }

    // Parse JSON response and extract the phone_from field
    ESP_LOGI(TAG, "Parsing JSON response...");
    cJSON *root = cJSON_Parse(json_start);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response: %s", json_start);  // Print the response if parsing fails
    } else {
        cJSON *phone_from = cJSON_GetObjectItem(root, "phone_from");
        if (phone_from == NULL) {
            ESP_LOGE(TAG, "Failed to get phone_from field");
        } else {
            strncpy(phone_from_data, phone_from->valuestring, sizeof(phone_from_data) - 1);
            phone_from_data[sizeof(phone_from_data) - 1] = '\0';  // Ensure null-termination
            ESP_LOGI(TAG, "Stored phone_from: %s", phone_from_data);

            // Store phone_from data in NVS
            nvs_handle_t nvs_handle;
            esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
            if (err == ESP_OK) {
                err = nvs_set_str(nvs_handle, "phone_from", phone_from_data);
                if (err == ESP_OK) {
                    nvs_commit(nvs_handle);
                    ESP_LOGI(TAG, "phone_from data stored in NVS");
                }
                nvs_close(nvs_handle);
            }
        }
        cJSON_Delete(root);
    }

    uart_send("AT+HTTPTERM");
    uart_receive(buf, BUF_SIZE);
}


void configure_sms() {
    uart_send("AT+CMGF=1");  // Set SMS text mode
    uart_receive(NULL, 0);   // Wait for acknowledgment
    uart_send("AT+CNMI=2,2,0,0,0");  // Configure SMS notifications
    uart_receive(NULL, 0);   // Wait for acknowledgment
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

                                ESP_LOGI(TAG, "Extracted Phone: %s", phone_start);
                                ESP_LOGI(TAG, "Extracted SMS: %s", sms_start);
                                
                                // Check for reset command
                                if (strcmp(sms_start, "reset") == 0) {
                                    ESP_LOGI(TAG, "Reset command received, restarting...");
                                    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Short delay before reset
                                    esp_restart();
                                }

                                // Prepare the POST data
                                cJSON *root = cJSON_CreateObject();
                                cJSON_AddStringToObject(root, "phone_from", PHONE_FROM);
                                cJSON_AddStringToObject(root, "phone_to", phone_start);
                                cJSON_AddStringToObject(root, "message", sms_start);
    
                                char *post_data = cJSON_Print(root);
                                // Send POST request
                                http_post_request(HTTP_URL, post_data);

                                // Clean up
                                cJSON_Delete(root);
                                free(post_data);
                            }
                        }
                    }
                }
            }
        }
    }
}

// Function to read phone_from data from NVS
void read_phone_from_nvs() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size;
        err = nvs_get_str(nvs_handle, "phone_from", NULL, &required_size);
        if (err == ESP_OK && required_size > 0) {
            err = nvs_get_str(nvs_handle, "phone_from", phone_from_data, &required_size);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Read phone_from data from NVS: %s", phone_from_data);
            }
        }
        nvs_close(nvs_handle);
    }
}
void read_and_print_nvs_data() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    } else {
        // Read the size of the stored string
        size_t required_size;
        err = nvs_get_str(nvs_handle, "phone_from", NULL, &required_size);
        if (err == ESP_OK) {
            char* phone_from_data = malloc(required_size);
            err = nvs_get_str(nvs_handle, "phone_from", phone_from_data, &required_size);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Stored phone_from: %s", phone_from_data);
                printf("defx");
            } else {
                ESP_LOGE(TAG, "Error (%s) reading phone_from from NVS!", esp_err_to_name(err));
            }
            free(phone_from_data);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "phone_from not found in NVS");
        } else {
            ESP_LOGE(TAG, "Error (%s) getting phone_from size!", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
    }
}

// Main task with increased stack size
void main_task(void *pvParameters) {
    uart_init();
    ESP_LOGI(TAG, "Starting up");
    
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    connect_gprs();
    read_phone_from_nvs();  // Read phone_from data from NVS
    http_get_request(HTTP_URL);
    configure_sms();  // Configure SMS settings
    xTaskCreate(sms_task, "sms_task", 8192, NULL, 5, NULL);  // Increased stack size to 8192
    read_and_print_nvs_data();
    ESP_LOGI(TAG, "Operation completed");

    vTaskDelete(NULL);  // Delete the main_task to free resources
}

void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(main_task, "main_task", 8192, NULL, 5, NULL);  // Run main_task with larger stack size
}
