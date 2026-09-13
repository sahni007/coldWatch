/*
 * sms_module.h
 * SRS1_006 / SRS1_007 / SRS1_010: "Send the alarm via SMS"
 * Uses a SIM800L/SIM900-style AT-command GSM module over ESP32 UART.
 */
#ifndef COLDWATCH_SMS_MODULE_H
#define COLDWATCH_SMS_MODULE_H

#include <stdbool.h>

void sms_module_init(void);
bool sms_module_send(const char *message);

#endif // COLDWATCH_SMS_MODULE_H

