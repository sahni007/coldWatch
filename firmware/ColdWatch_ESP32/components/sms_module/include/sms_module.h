/*
 * sms_module.h
 * SRS1_006 / SRS1_007 / SRS1_010: "Send the alarm via SMS"
 * Uses a SIM800L/SIM900-style AT-command GSM module over ESP32 UART.
 */
#ifndef COLDWATCH_SMS_MODULE_H
#define COLDWATCH_SMS_MODULE_H

#include <stdbool.h>
#include <stdint.h>

// ---- GSM screen support (multi-screen touch UI) ----
typedef enum {
    GSM_STATE_UNKNOWN = 0,       // not polled yet since boot
    GSM_STATE_INIT_FAILED = 1,   // module didn't respond to AT during sms_module_init()
    GSM_STATE_NOT_REGISTERED = 2,// AT+CREG? reports not registered on the network
    GSM_STATE_REGISTERED = 3,    // AT+CREG? reports registered (home or roaming)
} gsm_state_t;

void sms_module_init(void);
bool sms_module_send(const char *message);

// Call periodically (e.g. every GSM_STATUS_POLL_INTERVAL_MS) to refresh the
// network registration + signal quality shown on the GSM screen.
void sms_module_poll_status(void);

gsm_state_t sms_module_get_state(void);
// Signal quality 0-31 (higher = better), or -1 if never successfully polled.
int8_t sms_module_get_signal_quality(void);
// Result of the most recent sms_module_send() call (true = OK), for display.
bool sms_module_get_last_send_ok(void);
// True once at least one sms_module_send() call has been made since boot
// (so the GSM screen can show "--" instead of a misleading FAILED/OK).
bool sms_module_get_has_sent_any(void);

#endif // COLDWATCH_SMS_MODULE_H

