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
#include "driver/gpio.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#define UART_NUM UART_NUM_1
#define TXD_PIN 17
#define RXD_PIN 16
#define BUF_SIZE 1024
#define SMS_QUEUE_SIZE 10
#define LED_PIN 4
#define LED_PIN_1 15
#define RESET_NOTIFY_NUMBER "+919409277556"  // Replace with your phone number

int parse_status_code(const char *response);
void handle_http_status_code(int status_code);
void blinkLED(int interval_ms);
static const char *TAG = "SIM800L";
static QueueHandle_t sms_queue;

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
bool readSIM800Response(char *buffer, int buffer_len) {
    // Implement code to read the response from the SIM800L here
    // This function should populate the 'buffer' with the SIM800L's response
    // For this example, we'll simulate the +HTTPACTION response for 302 redirection
    snprintf(buffer, buffer_len, "+HTTPACTION: 0,302,150");
    return true;
}
void sendCommand(const char* cmd, unsigned long waitTime, char *responseBuffer, size_t responseSize) {
    // Send the command via UART
    uart_write_bytes(UART_NUM, cmd, strlen(cmd));
    uart_write_bytes(UART_NUM, "\r\n", 2);  // Send carriage return and newline
    vTaskDelay(waitTime / portTICK_PERIOD_MS);  // Wait for the response

    uint8_t data[1024];
    int len = uart_read_bytes(UART_NUM, data, sizeof(data) - 1, waitTime / portTICK_PERIOD_MS);
     // If data is received, null-terminate and store the response
    if (len > 0) {
        data[len] = '\0';  // Null-terminate the received data
        snprintf(responseBuffer, responseSize, "%s", data);  // Copy to response buffer
        printf("Received: %s\n", responseBuffer);  // Print the received data
    } else {
        responseBuffer[0] = '\0';  // No data received, empty the buffer
    }
}
void initLED() {
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(LED_PIN_1);
    gpio_set_direction(LED_PIN_1, GPIO_MODE_OUTPUT);
}
void connect_gprs() {
    char buf[BUF_SIZE];
    uart_send("AT");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+CMGF=1");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+CNMI=2,2,0,0,0");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=3,1,\"APN\",\"airtelgprs.com\""); // Replace 'airtelgprs.com' with your SIM's APN
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=1,1");
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+SAPBR=2,1");
    uart_receive(buf, BUF_SIZE);
     if (strstr(buf, "OK")) {
        gpio_set_level(LED_PIN_1, 1);  // Set LED_PIN high
    } else {
        gpio_set_level(LED_PIN_1, 0);  // Set LED_PIN low
    }
}
void http_post_request(const char* url, const char *post_data) {
    char buf[BUF_SIZE];
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
    char response[1024];
    uart_send("AT+HTTPINIT");
    uart_receive(buf, BUF_SIZE);
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
    char at_command[512];
    snprintf(at_command, sizeof(at_command), "AT+HTTPPARA=\"USERDATA\",\"%s\"", auth_header);
    sendCommand(at_command, 1000, response, sizeof(response));
    uart_send("AT+HTTPACTION=1");
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for the response
    uart_receive(buf, BUF_SIZE);
    uart_send("AT+HTTPTERM");
    uart_receive(buf, BUF_SIZE);
}
void send_basic_auth_request() {
	connect_gprs();
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
    char response[1024];
    // Send the HTTP request
    sendCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", 1000, response, sizeof(response));
    sendCommand("AT+SAPBR=3,1,\"APN\",\"airtelgprs.com\"", 1000, response, sizeof(response));
    sendCommand("AT+SAPBR=2,1", 2000, response, sizeof(response));
    sendCommand("AT+HTTPINIT", 1000, response, sizeof(response));
    sendCommand("AT+HTTPPARA=\"CID\",1", 1000, response, sizeof(response));
    sendCommand("AT+HTTPPARA=\"URL\",\"http://one-phone-backend-6adbf4e679b0.herokuapp.com/api/devices\"", 1000, response, sizeof(response));

    // Set the Authorization header
    char at_command[512];
    snprintf(at_command, sizeof(at_command), "AT+HTTPPARA=\"USERDATA\",\"%s\"", auth_header);
    sendCommand(at_command, 1000, response, sizeof(response));

    // Perform the GET request
    sendCommand("AT+HTTPACTION=0", 1000, response, sizeof(response));
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    
    if (readSIM800Response(response, sizeof(response))) {
        // Parse status code from the response
        int status_code = parse_status_code(response);
        handle_http_status_code(status_code);
    }
    sendCommand("AT+HTTPREAD", 1000, response, sizeof(response));
    printf("HTTP Body: %s\n", response);  // Print the HTTP body (for debugging)

    // Terminate the session
    sendCommand("AT+HTTPTERM", 1000, response, sizeof(response));
   
}
// Function to parse the HTTP status code from the AT+HTTPACTION response
int parse_status_code(const char *response) {
    // Look for "+HTTPACTION" in the response
    char *start = strstr(response, "+HTTPACTION");
    if (start != NULL) {
        start = strchr(start, ',');  // Find the first comma
       // if (start != NULL) {
           // start = strchr(start + 1, ',');  // Find the second comma
            if (start != NULL) {
                return atoi(start + 1);  // Convert the status code to an integer
            }
        
    }
    return -1;  // Return -1 if no status code found
}

// Task to blink the LED for a specific duration


// Function to handle the HTTP status codes
void handle_http_status_code(int status_code) {

    switch (status_code) {
        case 200:
            printf("HTTP Status 200: OK\n");
            // Stop blinking or keep LED on to indicate success
            gpio_set_level(LED_PIN, 1); // LED ON (constant)
            break;
        case 302:
            printf("HTTP Status 302: Redirected\n");
            // Handle redirection (slow blink)
            blinkLED(100);  // Slow blink (1000ms interval)
            break;
        case 404:
            printf("HTTP Status 404: Not Found\n");
            // Handle error (fast blink)
            blinkLED(200);   // Fast blink (200ms interval)
            break;
        case 500:
            printf("HTTP Status 500: Internal Server Error\n");
            // Handle server error (rapid blink)
            blinkLED(1000);   // Very fast blink (100ms interval)
            break;
        default:
            printf("HTTP Status %d: Unhandled status code\n", status_code);
            gpio_set_level(LED_PIN, 0);  // Turn off LED
            break;
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
                                   
                                    // Check for reset command
                                    if (strcmp(sms_start, "reset") == 0) {
                                    ESP_LOGI(TAG, "Reset command received, sending notification and restarting...");
                                    send_sms(RESET_NOTIFY_NUMBER, "ESP32 is resetting...");  // Send SMS notification
                                    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Short delay before reset
                                    esp_restart();
                                   }

                                // Send the phone and SMS to the queue
                                cJSON *root = cJSON_CreateObject();
                                //cJSON_AddStringToObject(root, "phone_from", "8849384442");
                                cJSON_AddStringToObject(root, "phone_to", phone);
                                cJSON_AddStringToObject(root, "message_text", sms);
                                char *post_data = cJSON_Print(root);

                                // Send the POST request to the queue
                                xQueueSend(sms_queue, &post_data, portMAX_DELAY);

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

void http_task(void *pvParameters) {
    char *post_data;
    while (1) {
        if (xQueueReceive(sms_queue, &post_data, portMAX_DELAY)) {
            http_post_request("http://one-phone-backend-6adbf4e679b0.herokuapp.com/api/messages", post_data);
            free(post_data);  // Free the POST data after sending
        }
    }
}
void http_get_task(void *pvParameters) {
    while (1)
     {
        send_basic_auth_request ("http://one-phone-backend-6adbf4e679b0.herokuapp.com/api/devices"); // Replace with your URL
        vTaskDelay(6000000 / portTICK_PERIOD_MS); // Wait for 60 seconds before the next GET request
    }
}
  // Simulate handling of different HTTP status codes
 void blinkLED(int interval_ms) {
    while (1) {
        gpio_set_level(LED_PIN, 1); // LED ON
        vTaskDelay(interval_ms / portTICK_PERIOD_MS); // Wait for interval
        gpio_set_level(LED_PIN, 0); // LED OFF
        vTaskDelay(interval_ms / portTICK_PERIOD_MS); // Wait for interval
    }
}
void app_main() {
    init_uart();
    initLED(); // Initialize the LED 
    ESP_LOGI(TAG, "Starting up"); 
    sms_queue = xQueueCreate(SMS_QUEUE_SIZE, sizeof(char *));
    if (sms_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create SMS queue");
        return;
     }
      // Simulate handling of different HTTP status codes
   
     connect_gprs();
     xTaskCreate(sms_task, "sms_task", 8192, NULL, 10, NULL);  // Increased stack size to 8192
     xTaskCreate(http_task, "http_task", 8192, NULL, 8, NULL);  // Task to handle HTTP POST requests
     xTaskCreate(http_get_task, "http_get_task", 8192, NULL, 5, NULL);  // Task to handle HTTP GET requests
     // Send the HTTP request
     //blinkled();
}
