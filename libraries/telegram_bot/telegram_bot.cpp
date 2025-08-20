#include "telegram_bot.h"
#include "../damper_control/damper_control.h"
#include "../temperature/temperature.h"
#include "../wifi/wifi.h"
#include "cJSON.h"
#include "esp_crt_bundle.h" // TLS certificate bundle
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- PATCH: konfigurācija un glabājamie ---
#ifndef MAX_HTTP_RECV_BUFFER
#define MAX_HTTP_RECV_BUFFER 8192 // bija 2048
#endif

// STEIDZAMI: izņem BOT_TOKEN fallback no repo un nodod via build_flags vai NVS
#ifndef BOT_TOKEN
#warning "BOT_TOKEN nav nodefinēts build laikā. " \
         "Pievieno -DBOT_TOKEN vai ielādē no NVS."
#endif

static const char *TAG = "TELEGRAM_BOT";

// Izejošo ziņu rinda (izvairāmies no bloķēšanas biznesa pusē)
typedef struct {
  char chat_id[32];
  char text[256];
  char reply_markup[640];
  bool has_markup;
} out_msg_t;

static QueueHandle_t g_tx_queue = NULL;

// Pastāvīgi HTTP klienti GET un POST plūsmām (keep-alive, viena instance per
// task)
static esp_http_client_handle_t g_client_get = NULL;
static esp_http_client_handle_t g_client_post = NULL;

// Last update id long-pollingam
static int64_t last_update_id = 0;

// Droša bufera aizpilde (ar pārpildes aizsardzību)
static char response_buffer[MAX_HTTP_RECV_BUFFER + 1];
static volatile bool response_overflow = false;

// Globālie (no citām bibliotēkām)
extern int target_temp_c;
extern int temperature;
extern int kP;
extern int temperature_min;
extern std::string messageDamp;

// State
static bool waiting_for_temp = false;
static bool waiting_for_kp = false;
static bool waiting_for_temp_min = false;

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  static int output_len = 0;
  switch (evt->event_id) {
  case HTTP_EVENT_ON_DATA:
    if (output_len + (int)evt->data_len < MAX_HTTP_RECV_BUFFER) {
      memcpy(response_buffer + output_len, evt->data, evt->data_len);
      output_len += evt->data_len;
      response_buffer[output_len] = '\0';
    } else {
      response_overflow = true; // iezīmē, ka pienācis par daudz
    }
    break;
  case HTTP_EVENT_ON_FINISH:
    output_len = 0;
    break;
  default:
    break;
  }
  return ESP_OK;
}

// Forward decl
static esp_err_t send_telegram_message(const char *chat_id, const char *text,
                                       const char *reply_markup);
static void process_message(cJSON *message);
static void process_callback_query(cJSON *callback_query);
static esp_err_t ensure_post_client();
static esp_err_t ensure_get_client();
static esp_err_t get_telegram_updates();
static void sender_task(void *pv);
static void telegram_task(void *pv);

// enqueue; faktisko POST veic sender_task
static esp_err_t send_telegram_message(const char *chat_id, const char *text,
                                       const char *reply_markup) {
  if (!g_tx_queue)
    return ESP_FAIL;
  out_msg_t m = {};
  strncpy(m.chat_id, chat_id ? chat_id : "", sizeof(m.chat_id) - 1);
  strncpy(m.text, text ? text : "", sizeof(m.text) - 1);
  m.has_markup = reply_markup && *reply_markup;
  if (m.has_markup) {
    strncpy(m.reply_markup, reply_markup, sizeof(m.reply_markup) - 1);
  }
  return xQueueSend(g_tx_queue, &m, pdMS_TO_TICKS(10)) == pdTRUE ? ESP_OK
                                                                 : ESP_FAIL;
}

static esp_err_t answer_callback_query(const char *callback_query_id) {
  if (ensure_post_client() != ESP_OK)
    return ESP_FAIL;
  response_overflow = false;
  memset(response_buffer, 0, sizeof(response_buffer));

  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "callback_query_id", callback_query_id);
  char *payload = cJSON_PrintUnformatted(json);

  char url[192];
  snprintf(url, sizeof(url),
           "https://api.telegram.org/bot%s/answerCallbackQuery", BOT_TOKEN);
  esp_http_client_set_url(g_client_post, url);
  esp_http_client_set_method(g_client_post, HTTP_METHOD_POST);
  esp_http_client_set_post_field(g_client_post, payload, strlen(payload));

  esp_err_t err = esp_http_client_perform(g_client_post);

  free(payload);
  cJSON_Delete(json);
  return err;
}

static esp_err_t ensure_post_client() {
  if (g_client_post)
    return ESP_OK;
  esp_http_client_config_t cfg = {};
  cfg.url = "https://api.telegram.org"; // pamata; pilno URL iestatīsim runtime
  cfg.method = HTTP_METHOD_POST;
  cfg.event_handler = http_event_handler;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 40000;       // ≥ getUpdates timeout + rezervē
  cfg.keep_alive_enable = true; // svarīgi
  g_client_post = esp_http_client_init(&cfg);
  if (!g_client_post)
    return ESP_FAIL;
  esp_http_client_set_header(g_client_post, "Connection", "keep-alive");
  esp_http_client_set_header(g_client_post, "Content-Type", "application/json");
  return ESP_OK;
}

static void sender_task(void *pv) {
  const uint32_t MAX_BACKOFF_MS = 10000;
  uint32_t backoff_ms = 0;
  while (1) {
    out_msg_t m;
    if (xQueueReceive(g_tx_queue, &m, portMAX_DELAY) != pdTRUE)
      continue;

    if (ensure_post_client() != ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // JSON sagatavošana
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "chat_id", m.chat_id);
    cJSON_AddStringToObject(json, "text", m.text);
    if (m.has_markup) {
      cJSON *markup = cJSON_Parse(m.reply_markup);
      if (markup)
        cJSON_AddItemToObject(json, "reply_markup", markup);
    }
    char *payload = cJSON_PrintUnformatted(json);

    // URL uz /sendMessage
    char url[192];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage",
             BOT_TOKEN);
    esp_http_client_set_url(g_client_post, url);
    esp_http_client_set_method(g_client_post, HTTP_METHOD_POST);
    esp_http_client_set_post_field(g_client_post, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(g_client_post);
    if (err == ESP_OK) {
      backoff_ms = 0; // success -> notīra backoff
    } else {
      // neveiksmes gadījumā iemetam ziņu atpakaļ rindas sākumā un backoff
      xQueueSendToFront(g_tx_queue, &m, 0);
      backoff_ms = backoff_ms ? (backoff_ms * 2) : 200;
      if (backoff_ms > MAX_BACKOFF_MS)
        backoff_ms = MAX_BACKOFF_MS;
      // jitter
      vTaskDelay(pdMS_TO_TICKS(backoff_ms + (esp_random() % 200)));
    }

    free(payload);
    cJSON_Delete(json);
  }
}

static esp_err_t ensure_get_client() {
  if (g_client_get)
    return ESP_OK;
  esp_http_client_config_t cfg = {};
  cfg.url = "https://api.telegram.org"; // pilno URL iestatīsim katru reizi
  cfg.method = HTTP_METHOD_GET;
  cfg.event_handler = http_event_handler;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 45000; // ≥ BotAPI timeout + 10s
  cfg.keep_alive_enable = true;
  g_client_get = esp_http_client_init(&cfg);
  if (!g_client_get)
    return ESP_FAIL;
  esp_http_client_set_header(g_client_get, "Connection", "keep-alive");
  return ESP_OK;
}

static esp_err_t get_telegram_updates() {
  if (ensure_get_client() != ESP_OK)
    return ESP_FAIL;
  response_overflow = false;
  memset(response_buffer, 0, sizeof(response_buffer));

  // allowed_updates=["message","callback_query"] URL-enkodēts
  const char *allowed = "%5B%22message%22%2C%22callback_query%22%5D";
  // timeout=30, limit=1, offset=last_update_id+1
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.telegram.org/bot%s/"
           "getUpdates?timeout=30&limit=1&allowed_updates=%s&offset=%lld",
           BOT_TOKEN, allowed, (long long)(last_update_id + 1));

  esp_http_client_set_url(g_client_get, url);
  esp_http_client_set_method(g_client_get, HTTP_METHOD_GET);

  esp_err_t err = esp_http_client_perform(g_client_get);
  if (err != ESP_OK)
    return err;
  if (response_overflow) {
    ESP_LOGW(TAG, "Telegram response truncated (buffer=%d)",
             MAX_HTTP_RECV_BUFFER);
  }

  cJSON *json = cJSON_Parse(response_buffer);
  if (!json)
    return ESP_FAIL;
  cJSON *ok = cJSON_GetObjectItem(json, "ok");
  if (cJSON_IsTrue(ok)) {
    cJSON *result = cJSON_GetObjectItem(json, "result");
    if (cJSON_IsArray(result)) {
      cJSON *update = NULL;
      cJSON_ArrayForEach(update, result) {
        cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
        if (update_id && update_id->valuedouble > last_update_id) {
          last_update_id = (int64_t)update_id->valuedouble;
          cJSON *message = cJSON_GetObjectItem(update, "message");
          cJSON *callback_query = cJSON_GetObjectItem(update, "callback_query");
          if (message)
            process_message(message);
          else if (callback_query)
            process_callback_query(callback_query);
        }
      }
    }
  }
  cJSON_Delete(json);
  return ESP_OK;
}

static void telegram_task(void *pv) {
  ESP_LOGI(TAG, "Telegram task started");
  const uint32_t MAX_BACKOFF_MS = 10000;
  uint32_t backoff_ms = 0;

  while (1) {
    if (!is_wifi_connected()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    esp_err_t err = get_telegram_updates();
    if (err == ESP_OK) {
      backoff_ms = 0; // success => tūlīt nākamais long-poll
      continue;
    }
    ESP_LOGE(TAG, "getUpdates error: %s", esp_err_to_name(err));
    backoff_ms = backoff_ms ? (backoff_ms * 2) : 200;
    if (backoff_ms > MAX_BACKOFF_MS)
      backoff_ms = MAX_BACKOFF_MS;
    vTaskDelay(pdMS_TO_TICKS(backoff_ms + (esp_random() % 200)));
  }
}

static char *create_main_keyboard() {
  cJSON *keyboard = cJSON_CreateObject();
  cJSON *inline_keyboard = cJSON_CreateArray();

  // Row 1
  cJSON *row1 = cJSON_CreateArray();
  cJSON *btn1 = cJSON_CreateObject();
  cJSON_AddStringToObject(btn1, "text", "🔄 Info");
  cJSON_AddStringToObject(btn1, "callback_data", "refresh");
  cJSON_AddItemToArray(row1, btn1);
  cJSON_AddItemToArray(inline_keyboard, row1);

  // Row 2
  cJSON *row2 = cJSON_CreateArray();
  cJSON *bt_temp = cJSON_CreateObject();
  cJSON_AddStringToObject(bt_temp, "text", "🌡️ Set Target");
  cJSON_AddStringToObject(bt_temp, "callback_data", "change_temp");
  cJSON_AddItemToArray(row2, bt_temp);
  cJSON *bt_kp = cJSON_CreateObject();
  cJSON_AddStringToObject(bt_kp, "text", "⚙️ kP");
  cJSON_AddStringToObject(bt_kp, "callback_data", "change_kp");
  cJSON_AddItemToArray(row2, bt_kp);
  cJSON_AddItemToArray(inline_keyboard, row2);

  // Row 3
  cJSON *row3 = cJSON_CreateArray();
  cJSON *bt_min = cJSON_CreateObject();
  cJSON_AddStringToObject(bt_min, "text", "❄️ Min Temp");
  cJSON_AddStringToObject(bt_min, "callback_data", "change_temp_min");
  cJSON_AddItemToArray(row3, bt_min);
  cJSON_AddItemToArray(inline_keyboard, row3);

  cJSON_AddItemToObject(keyboard, "inline_keyboard", inline_keyboard);
  char *out = cJSON_PrintUnformatted(keyboard);
  cJSON_Delete(keyboard);
  return out;
}

static void process_message(cJSON *message) {
  cJSON *chat = cJSON_GetObjectItem(message, "chat");
  cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
  cJSON *text = cJSON_GetObjectItem(message, "text");
  if (!chat_id)
    return;

  char chat_id_str[32];
  snprintf(chat_id_str, sizeof(chat_id_str), "%lld",
           (long long)chat_id->valuedouble);

  if (text && text->valuestring) {
    ESP_LOGI(TAG, "RX: %s", text->valuestring);
    if (strcmp(text->valuestring, "/info") == 0) {
      char *kb = create_main_keyboard();
      send_telegram_message(chat_id_str, "🏠 Izvēlies darbību:", kb);
      free(kb);
    } else if (strcmp(text->valuestring, "/time") == 0) {
      char buf[48];
      snprintf(buf, sizeof(buf), "🕐 Laiks: %s", get_time_str());
      send_telegram_message(chat_id_str, buf, NULL);
    } else if (waiting_for_temp &&
               isdigit((unsigned char)text->valuestring[0])) {
      set_target_temperature(atoi(text->valuestring));
      waiting_for_temp = false;
      char msg[48];
      snprintf(msg, sizeof(msg), "✅ Target: %d°C", target_temp_c);
      send_telegram_message(chat_id_str, msg, NULL);
    } else if (waiting_for_kp && isdigit((unsigned char)text->valuestring[0])) {
      kP = atoi(text->valuestring);
      waiting_for_kp = false;
      char msg[32];
      snprintf(msg, sizeof(msg), "✅ kP: %d", kP);
      send_telegram_message(chat_id_str, msg, NULL);
    } else if (waiting_for_temp_min &&
               isdigit((unsigned char)text->valuestring[0])) {
      temperature_min = atoi(text->valuestring);
      waiting_for_temp_min = false;
      char msg[40];
      snprintf(msg, sizeof(msg), "✅ Min: %d°C", temperature_min);
      send_telegram_message(chat_id_str, msg, NULL);
    } else {
      send_telegram_message(chat_id_str, "❌ Nederīga ievade.", NULL);
    }
  }
}

static void process_callback_query(cJSON *callback_query) {
  cJSON *id = cJSON_GetObjectItem(callback_query, "id");
  cJSON *data = cJSON_GetObjectItem(callback_query, "data");
  cJSON *message = cJSON_GetObjectItem(callback_query, "message");
  if (!id || !data || !message)
    return;
  cJSON *chat = cJSON_GetObjectItem(message, "chat");
  cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
  if (!chat_id)
    return;

  char chat_id_str[32];
  snprintf(chat_id_str, sizeof(chat_id_str), "%lld",
           (long long)chat_id->valuedouble);
  answer_callback_query(id->valuestring);

  if (!data->valuestring)
    return;
  if (strcmp(data->valuestring, "refresh") == 0) {
    char status[256];
    snprintf(status, sizeof(status),
             "🔥 KRĀSNS STATUS 🔥\n\n"
             "🌡️ Temp: %d°C\n"
             "🎯 Target: %d°C\n"
             "⚙️ kP: %d\n"
             "❄️ Min: %d°C\n"
             "🎚️ Damper: %s",
             temperature, target_temp_c, kP, temperature_min,
             messageDamp.c_str());
    send_telegram_message(chat_id_str, status, NULL);
  } else if (strcmp(data->valuestring, "change_temp") == 0) {
    waiting_for_temp = true;
    waiting_for_kp = waiting_for_temp_min = false;
    send_telegram_message(chat_id_str, "🌡️ Ievadi mērķa temperatūru:", NULL);
  } else if (strcmp(data->valuestring, "change_kp") == 0) {
    waiting_for_kp = true;
    waiting_for_temp = waiting_for_temp_min = false;
    send_telegram_message(chat_id_str, "⚙️ Ievadi kP vērtību:", NULL);
  } else if (strcmp(data->valuestring, "change_temp_min") == 0) {
    waiting_for_temp_min = true;
    waiting_for_temp = waiting_for_kp = false;
    send_telegram_message(chat_id_str, "❄️ Ievadi min. temperatūru:", NULL);
  }
}
void telegram_bot_start() {
  if (!g_tx_queue)
    g_tx_queue = xQueueCreate(16, sizeof(out_msg_t));
  xTaskCreate(telegram_task, "telegram_task", 12288, NULL, 5, NULL);
  xTaskCreate(sender_task, "telegram_sender", 12288, NULL, 5, NULL);
  ESP_LOGI(TAG, "Telegram bot started (long-poll + tx-queue)");
}

void send_alert_message(const char *alert_text) {
  const char *CHAT_ID = "YOUR_CHAT_ID_HERE"; // TODO persist
  char buf[160];
  snprintf(buf, sizeof(buf), "🚨 BRĪDINĀJUMS: %s", alert_text);
  send_telegram_message(CHAT_ID, buf, NULL);
}
