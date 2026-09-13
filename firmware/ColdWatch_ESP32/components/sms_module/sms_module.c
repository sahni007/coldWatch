/*
 * sms_module.c
 */
#include "sms_module.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SMS_MODULE";
#define UART_RX_BUF_SIZE 256

static bool wait_for_response(const char *expected, uint32_t timeout_ms) {
    char resp[UART_RX_BUF_SIZE] = {0};
    size_t total = 0;
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        uint8_t byte;
        int len = uart_read_bytes(GSM_UART_PORT, &byte, 1, pdMS_TO_TICKS(50));
        if (len > 0 && total < sizeof(resp) - 1) {
            resp[total++] = (char)byte;
            resp[total] = '\0';
            if (strstr(resp, expected) != NULL) return true;
        }
    }
    return false;
}

void sms_module_init(void) {
    uart_config_t uart_config = {
        .baud_rate = GSM_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(GSM_UART_PORT, UART_RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(GSM_UART_PORT, &uart_config);
    uart_set_pin(GSM_UART_PORT, GSM_UART_TX_GPIO, GSM_UART_RX_GPIO,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    vTaskDelay(pdMS_TO_TICKS(1000));
    uart_write_bytes(GSM_UART_PORT, "AT\r\n", 4);
    wait_for_response("OK", 2000);
    uart_write_bytes(GSM_UART_PORT, "AT+CMGF=1\r\n", 11); // text mode
    wait_for_response("OK", 2000);
}

bool sms_module_send(const char *message) {
    ESP_LOGI(TAG, "Sending SMS: %s", message);

    uart_write_bytes(GSM_UART_PORT, "AT+CMGF=1\r\n", 11);
    if (!wait_for_response("OK", 2000)) {
        ESP_LOGW(TAG, "Module not responding to AT+CMGF");
        return false;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\r\n", SMS_DESTINATION_NUMBER);
    uart_write_bytes(GSM_UART_PORT, cmd, strlen(cmd));
    if (!wait_for_response(">", 3000)) {
        ESP_LOGW(TAG, "Module did not return prompt '>'");
        return false;
    }

    uart_write_bytes(GSM_UART_PORT, message, strlen(message));
    uint8_t ctrl_z = 0x1A;
    uart_write_bytes(GSM_UART_PORT, (const char *)&ctrl_z, 1);

    bool ok = wait_for_response("OK", SMS_SEND_TIMEOUT_MS);
    ESP_LOGI(TAG, "%s", ok ? "SMS sent successfully" : "SMS send failed / timeout");
    return ok;
}


