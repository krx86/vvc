#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Starts Telegram bot (long-polling + TX queue). Call after Wi-Fi/NVS init.
void telegram_bot_start();

// Enqueue alert message to be sent asynchronously.
void send_alert_message(const char *alert_text);

#ifdef __cplusplus
}
#endif
