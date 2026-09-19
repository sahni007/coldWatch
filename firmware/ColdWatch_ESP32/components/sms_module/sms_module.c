/*
 * sms_module.c
 */
#include "sms_module.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SMS_MODULE";
#define UART_RX_BUF_SIZE 256

// ---- GSM screen state (multi-screen touch UI) ----
static gsm_state_t s_gsm_state        = GSM_STATE_UNKNOWN;
static int8_t      s_signal_quality   = -1;    // 0-31, -1 = never polled
static bool        s_last_send_ok     = false;
static bool        s_has_sent_any     = false; // true after the first sms_module_send() call

// Reads bytes until 'expected' is seen in the accumulated response or the
// timeout elapses. If 'out_response' is non-NULL, the raw accumulated text
// is copied there (up to out_size-1 bytes) regardless of success, so the
// caller can parse fields like "+CREG: 0,1" or "+CSQ: 18,99" out of it.
static bool read_response(const char *expected, uint32_t timeout_ms,
                           char *out_response, size_t out_size) {
    char resp[UART_RX_BUF_SIZE] = {0};
    size_t total = 0;
    TickType_t start = xTaskGetTickCount();
    bool found = false;

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        uint8_t byte;
        int len = uart_read_bytes(GSM_UART_PORT, &byte, 1, pdMS_TO_TICKS(50));
        if (len > 0 && total < sizeof(resp) - 1) {
            resp[total++] = (char)byte;
            resp[total] = '\0';
            if (strstr(resp, expected) != NULL) {
                found = true;
                break;
            }
        }
    }

    if (out_response != NULL && out_size > 0) {
        strncpy(out_response, resp, out_size - 1);
        out_response[out_size - 1] = '\0';
    }
    return found;
}

static bool wait_for_response(const char *expected, uint32_t timeout_ms) {
    return read_response(expected, timeout_ms, NULL, 0);
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
    bool module_alive = wait_for_response("OK", 2000);
    uart_write_bytes(GSM_UART_PORT, "AT+CMGF=1\r\n", 11); // text mode
    wait_for_response("OK", 2000);

    // Seed the GSM screen's state; sms_module_poll_status() (called
    // periodically from main.c) refines this into REGISTERED/NOT_REGISTERED.
    s_gsm_state = module_alive ? GSM_STATE_UNKNOWN : GSM_STATE_INIT_FAILED;
}

bool sms_module_send(const char *message) {
    ESP_LOGI(TAG, "Sending SMS: %s", message);
    s_has_sent_any = true;

    uart_write_bytes(GSM_UART_PORT, "AT+CMGF=1\r\n", 11);
    if (!wait_for_response("OK", 2000)) {
        ESP_LOGW(TAG, "Module not responding to AT+CMGF");
        s_last_send_ok = false;
        return false;
    }

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"\r\n", SMS_DESTINATION_NUMBER);
    uart_write_bytes(GSM_UART_PORT, cmd, strlen(cmd));
    if (!wait_for_response(">", 3000)) {
        ESP_LOGW(TAG, "Module did not return prompt '>'");
        s_last_send_ok = false;
        return false;
    }

    uart_write_bytes(GSM_UART_PORT, message, strlen(message));
    uint8_t ctrl_z = 0x1A;
    uart_write_bytes(GSM_UART_PORT, (const char *)&ctrl_z, 1);

    bool ok = wait_for_response("OK", SMS_SEND_TIMEOUT_MS);
    ESP_LOGI(TAG, "%s", ok ? "SMS sent successfully" : "SMS send failed / timeout");
    s_last_send_ok = ok;
    return ok;
}

// SRS: GSM screen - network registration + signal quality. Called
// periodically (GSM_STATUS_POLL_INTERVAL_MS) from main.c, independent of
// SMS sending, so the GSM screen stays current even when no alarm has
// triggered an SMS recently.
void sms_module_poll_status(void) {
    char resp[96];

    // AT+CREG? reply looks like "+CREG: <n>,<stat>" where stat: 0=not
    // registered, 1=registered (home), 5=registered (roaming), anything
    // else = searching/denied/unknown.
    uart_write_bytes(GSM_UART_PORT, "AT+CREG?\r\n", 10);
    if (read_response("OK", 2000, resp, sizeof(resp))) {
        const char *p = strstr(resp, "+CREG:");
        if (p != NULL) {
            int n = 0, stat = -1;
            if (sscanf(p, "+CREG: %d,%d", &n, &stat) == 2) {
                s_gsm_state = (stat == 1 || stat == 5) ? GSM_STATE_REGISTERED
                                                        : GSM_STATE_NOT_REGISTERED;
            }
        }
    } else if (s_gsm_state != GSM_STATE_INIT_FAILED) {
        s_gsm_state = GSM_STATE_NOT_REGISTERED; // module stopped responding
    }

    // AT+CSQ reply looks like "+CSQ: <rssi>,<ber>" where rssi 0-31 (99 = unknown).
    uart_write_bytes(GSM_UART_PORT, "AT+CSQ\r\n", 8);
    if (read_response("OK", 2000, resp, sizeof(resp))) {
        const char *p = strstr(resp, "+CSQ:");
        if (p != NULL) {
            int rssi = 99, ber = 0;
            if (sscanf(p, "+CSQ: %d,%d", &rssi, &ber) >= 1 && rssi != 99) {
                s_signal_quality = (int8_t)rssi;
            } else {
                s_signal_quality = -1;
            }
        }
    }
}

gsm_state_t sms_module_get_state(void)          { return s_gsm_state; }
int8_t      sms_module_get_signal_quality(void) { return s_signal_quality; }
bool        sms_module_get_last_send_ok(void)   { return s_last_send_ok; }
bool        sms_module_get_has_sent_any(void)   { return s_has_sent_any; }

