/**
 * @file       webpage.c
 * @brief      Setup-Mode webpage interface (HTTP server + JSON API)
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-07-25
 *
 * @version    0.1  Initial Version
 */
/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "webpage/webpage.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "nvs/nvs_app.h"
#include "fps/fps.h"
#include "io/io.h"
#include "timer/timer.h"

/******************************************************************************/
/*** Macros and Defines                                                       */
/******************************************************************************/

#define WEBPAGE_MAX_URI_HANDLERS   24U
#define WEBPAGE_HTTPD_STACK_SIZE  8192U

#define ENROLL_STEP_TIMEOUT_MS   10000U
#define RESET_SCAN_TIMEOUT_MS    20000U

#define POST_REBOOT_DELAY_MS       500U
#define ENROLL_REBOOT_DELAY_MS    1000U
#define RESET_REBOOT_DELAY_MS     1000U
#define LED_RESTORE_DELAY_MS      1500U

/** Reset-device "scan" phase: delay between confirmed presence and the
 *  first fps_ScanStep() capture attempt. io_WaitFingerPresent() only
 *  proves contact started, not that the finger has fully/stably seated —
 *  capturing immediately produced a bad first image (fps_Scan rc=ERROR)
 *  followed by a poor-quality second attempt (rc=NO_MATCH), even for the
 *  correct, matchable admin finger (confirmed good via the PC test tool,
 *  which lets the user settle their finger before pressing Enter to
 *  scan). This gives the same settle time automatically. */
#define SCAN_CAPTURE_SETTLE_MS     600U

/** How long a result LED (op-success, scan-success, scan-failed) stays
 *  visible before moving on (SWS-FPM102/SWS-FPM104). There is no
 *  lift-detection to wait through first: performing a scan leaves the
 *  sensor unable to reliably report a lift afterward (confirmed
 *  independently via the vendor PC test tool, not just this firmware —
 *  see io_WaitFingerPresent() in io.c), so the result LED is just held
 *  for a fixed, honest duration and then the flow moves straight to
 *  "awaiting finger". The *next* capture/scan's own presence-wait still
 *  requires a genuinely fresh touch (io_WaitFingerPresent() waits for a
 *  real edge, not just a level), which is what actually enforces "the
 *  user must lift and place again". */
#define RESULT_DISPLAY_MS         1000U

/** Minimum time the "scanning" LED stays visible during an enrollment
 *  capture, once presence is confirmed and the actual capture attempt is
 *  underway (SWS-FPM102). A real capture can complete in well under this;
 *  the wait pads out to this minimum so the user reliably sees a distinct
 *  "scanning" moment rather than an instant flick from "awaiting finger"
 *  straight to the result — without ever showing "scanning" for longer
 *  than the real capture actually took. */
#define SCANNING_DISPLAY_MIN_MS   1000U

/* Embedded web assets — target_add_binary_data() (ESP-IDF utilities.cmake)
 * derives the symbol name from the embedded file's basename only (not its
 * full relative path), via get_filename_component(... NAME) + a
 * MAKE_C_IDENTIFIER pass: "index.html" -> "_binary_index_html_{start,end}". */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

/******************************************************************************/
/*** Types                                                                    */
/******************************************************************************/

typedef struct
{
  bool     armed;
  uint8_t  uuid;
  uint8_t  finger_id;
  uint8_t  function_code;

  /* Slot IDs committed so far in the current attempt (whether this is a
   * regular add-fingerprint session or the First-Run admin enrollment) —
   * tracked so an aborted/failed attempt can discard them rather than
   * leaving a partial (fewer than 3 templates) enrollment behind. */
  uint16_t committed_ids[3];
  uint8_t  committed_count;
} pending_enroll_t;

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

static const char *TAG = "webpage";

static httpd_handle_t   s_server = NULL;
static webpage_mode_t   s_mode;
static pending_enroll_t s_pending_enroll;

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

/* --- response/body helpers --- */
static esp_err_t send_json(httpd_req_t *req, cJSON *root);
static esp_err_t send_ok(httpd_req_t *req);
static esp_err_t send_error(httpd_req_t *req, const char *status_line,
                             const char *code, const char *message);
static esp_err_t bad_request(httpd_req_t *req, const char *code, const char *message);
static esp_err_t server_error(httpd_req_t *req, const char *code, const char *message);
static bool      read_json_body(httpd_req_t *req, size_t max_len, cJSON **o_json);
static bool      get_query_param(httpd_req_t *req, const char *key, char *o_val, size_t i_len);
static bool      get_query_uint(httpd_req_t *req, const char *key, unsigned *o_val);

static void timer_reboot_cb(void *arg);
static void timer_restore_diag0_cb(void *arg);
static void led_result_then_awaiting(void);
static void discard_session_templates(void);

/* --- static assets --- */
static esp_err_t handle_index(httpd_req_t *req);
static esp_err_t handle_app_js(httpd_req_t *req);
static esp_err_t handle_style_css(httpd_req_t *req);

/* --- mode --- */
static esp_err_t handle_get_mode(httpd_req_t *req);

/* --- system: general --- */
static esp_err_t handle_get_general(httpd_req_t *req);
static esp_err_t handle_post_general(httpd_req_t *req);

/* --- system: wifi --- */
static esp_err_t handle_get_wifi(httpd_req_t *req);
static esp_err_t handle_post_wifi(httpd_req_t *req);

/* --- system: mqtt --- */
static esp_err_t handle_get_mqtt(httpd_req_t *req);
static esp_err_t handle_post_mqtt(httpd_req_t *req);

/* --- system: reset / exit --- */
static esp_err_t handle_reset_start(httpd_req_t *req);
static esp_err_t handle_reset_scan(httpd_req_t *req);
static esp_err_t handle_exit(httpd_req_t *req);

/* --- users --- */
static esp_err_t handle_get_users(httpd_req_t *req);
static esp_err_t handle_post_user(httpd_req_t *req);
static esp_err_t handle_delete_user(httpd_req_t *req);

/* --- fingerprints --- */
static esp_err_t handle_get_fingerprints(httpd_req_t *req);
static esp_err_t handle_delete_fingerprint(httpd_req_t *req);

/* --- enroll wizard (add-fingerprint and first-run share the mechanics) --- */
static esp_err_t do_enroll_scan(httpd_req_t *req, uint8_t uuid, uint8_t finger_id,
                                 uint8_t function_code, bool is_first_run);
static esp_err_t handle_enroll_prepare(httpd_req_t *req);
static esp_err_t handle_enroll_scan(httpd_req_t *req);
static esp_err_t handle_enroll_cancel(httpd_req_t *req);
static esp_err_t handle_firstrun_scan(httpd_req_t *req);

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t webpage_Init(webpage_mode_t i_mode)
{
  s_mode = i_mode;
  memset(&s_pending_enroll, 0, sizeof(s_pending_enroll));

  httpd_config_t config  = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = WEBPAGE_MAX_URI_HANDLERS;
  config.stack_size       = WEBPAGE_HTTPD_STACK_SIZE;

  if (httpd_start(&s_server, &config) != ESP_OK)
  {
    ESP_LOGE(TAG, "httpd_start failed");
    return RC_ERROR;
  }

  static const httpd_uri_t handlers[] = {
    { .uri = "/",                               .method = HTTP_GET,    .handler = handle_index },
    { .uri = "/app.js",                         .method = HTTP_GET,    .handler = handle_app_js },
    { .uri = "/style.css",                      .method = HTTP_GET,    .handler = handle_style_css },
    { .uri = "/api/mode",                       .method = HTTP_GET,    .handler = handle_get_mode },
    { .uri = "/api/system/general",             .method = HTTP_GET,    .handler = handle_get_general },
    { .uri = "/api/system/general",             .method = HTTP_POST,   .handler = handle_post_general },
    { .uri = "/api/system/wifi",                .method = HTTP_GET,    .handler = handle_get_wifi },
    { .uri = "/api/system/wifi",                .method = HTTP_POST,   .handler = handle_post_wifi },
    { .uri = "/api/system/mqtt",                .method = HTTP_GET,    .handler = handle_get_mqtt },
    { .uri = "/api/system/mqtt",                .method = HTTP_POST,   .handler = handle_post_mqtt },
    { .uri = "/api/system/reset/start",         .method = HTTP_POST,   .handler = handle_reset_start },
    { .uri = "/api/system/reset/scan",          .method = HTTP_POST,   .handler = handle_reset_scan },
    { .uri = "/api/system/exit",                .method = HTTP_POST,   .handler = handle_exit },
    { .uri = "/api/users",                      .method = HTTP_GET,    .handler = handle_get_users },
    { .uri = "/api/users",                      .method = HTTP_POST,   .handler = handle_post_user },
    { .uri = "/api/users",                      .method = HTTP_DELETE, .handler = handle_delete_user },
    { .uri = "/api/fingerprints",                .method = HTTP_GET,    .handler = handle_get_fingerprints },
    { .uri = "/api/fingerprints",                .method = HTTP_DELETE, .handler = handle_delete_fingerprint },
    { .uri = "/api/fingerprints/enroll/prepare", .method = HTTP_POST,   .handler = handle_enroll_prepare },
    { .uri = "/api/fingerprints/enroll/scan",    .method = HTTP_POST,   .handler = handle_enroll_scan },
    { .uri = "/api/fingerprints/enroll/cancel",  .method = HTTP_POST,   .handler = handle_enroll_cancel },
    { .uri = "/api/firstrun/enroll/scan",        .method = HTTP_POST,   .handler = handle_firstrun_scan },
  };

  for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++)
  {
    if (httpd_register_uri_handler(s_server, &handlers[i]) != ESP_OK)
    {
      ESP_LOGE(TAG, "failed to register handler %s", handlers[i].uri);
      httpd_stop(s_server);
      s_server = NULL;
      return RC_ERROR;
    }
  }

  ESP_LOGI(TAG, "webpage server started (mode=%s)",
           (i_mode == WEBPAGE_MODE_FIRST_RUN) ? "first_run" : "normal_setup");
  return RC_SUCCESS;
}

RC_t webpage_Deinit(void)
{
  if (s_server != NULL)
  {
    httpd_stop(s_server);
    s_server = NULL;
  }
  return RC_SUCCESS;
}

/******************************************************************************/
/*** Local function implementation — response/body helpers                   */
/******************************************************************************/

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (out == NULL)
  {
    return server_error(req, "json_encode_failed", NULL);
  }
  httpd_resp_set_type(req, "application/json");
  esp_err_t rc = httpd_resp_send(req, out, (ssize_t)strlen(out));
  free(out);
  return rc;
}

static esp_err_t send_ok(httpd_req_t *req)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  return send_json(req, root);
}

static esp_err_t send_error(httpd_req_t *req, const char *status_line,
                             const char *code, const char *message)
{
  httpd_resp_set_status(req, status_line);
  httpd_resp_set_type(req, "application/json");

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "error", code);
  if (message != NULL)
  {
    cJSON_AddStringToObject(root, "message", message);
  }
  char *out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  esp_err_t rc = httpd_resp_send(req, out != NULL ? out : "{}",
                                  out != NULL ? (ssize_t)strlen(out) : 2);
  free(out);
  return rc;
}

static esp_err_t bad_request(httpd_req_t *req, const char *code, const char *message)
{
  return send_error(req, "400 Bad Request", code, message);
}

static esp_err_t server_error(httpd_req_t *req, const char *code, const char *message)
{
  return send_error(req, "500 Internal Server Error", code, message);
}

/**
 * @brief Read and parse a JSON request body.
 *
 * On failure, an error response has already been sent to @p req — the
 * caller should simply `return ESP_OK;` without sending anything else.
 */
static bool read_json_body(httpd_req_t *req, size_t max_len, cJSON **o_json)
{
  if (req->content_len == 0 || req->content_len > max_len)
  {
    bad_request(req, "body_too_large", NULL);
    return false;
  }

  char *buf = malloc(req->content_len + 1);
  if (buf == NULL)
  {
    server_error(req, "out_of_memory", NULL);
    return false;
  }

  size_t received = 0;
  while (received < req->content_len)
  {
    int ret = httpd_req_recv(req, buf + received, req->content_len - received);
    if (ret <= 0)
    {
      free(buf);
      bad_request(req, "recv_failed", NULL);
      return false;
    }
    received += (size_t)ret;
  }
  buf[received] = '\0';

  cJSON *json = cJSON_Parse(buf);
  free(buf);
  if (json == NULL)
  {
    bad_request(req, "invalid_json", NULL);
    return false;
  }

  *o_json = json;
  return true;
}

static bool get_query_param(httpd_req_t *req, const char *key, char *o_val, size_t i_len)
{
  char query[128];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
  {
    return false;
  }
  return httpd_query_key_value(query, key, o_val, i_len) == ESP_OK;
}

static bool get_query_uint(httpd_req_t *req, const char *key, unsigned *o_val)
{
  char val[16];
  if (!get_query_param(req, key, val, sizeof(val)))
  {
    return false;
  }
  char *end = NULL;
  unsigned long parsed = strtoul(val, &end, 10);
  if (end == val)
  {
    return false;
  }
  *o_val = (unsigned)parsed;
  return true;
}

static void timer_reboot_cb(void *arg)
{
  (void)arg;
  esp_restart();
}

static void timer_restore_diag0_cb(void *arg)
{
  (void)arg;
  fps_SetLed(FPM_LED_DIAG_0);
}

/**
 * @brief LED confirmation after any successful, non-final enrollment scan.
 *
 * Holds the op-success pulse for RESULT_DISPLAY_MS to confirm the capture,
 * then goes straight to "awaiting finger" for the next capture — no
 * lift-detection step (see RESULT_DISPLAY_MS above for why).
 */
static void led_result_then_awaiting(void)
{
  fps_SetLed(FPM_LED_OP_SUCCESS);
  vTaskDelay(pdMS_TO_TICKS(RESULT_DISPLAY_MS));
  fps_SetLed(FPM_LED_AWAITING_FINGER);
}

/**
 * @brief Discard every template committed so far in the current attempt.
 *
 * Called on any abort/failure path (lift-detection timeout, a commit
 * failure, or explicit user cancellation) so an incomplete enrollment
 * (fewer than 3 templates for the target finger) is never left behind in
 * the sensor's fingerprint library.
 */
static void discard_session_templates(void)
{
  if (s_pending_enroll.committed_count == 0)
  {
    return;
  }

  if (fps_Lock() == RC_SUCCESS)
  {
    for (uint8_t i = 0; i < s_pending_enroll.committed_count; i++)
    {
      fps_raw_delete(s_pending_enroll.committed_ids[i]);
    }
    fps_Unlock();
  }

  s_pending_enroll.committed_count = 0;
}

/******************************************************************************/
/*** Local function implementation — static assets                           */
/******************************************************************************/

/* EMBED_TXTFILES null-terminates each asset (unlike EMBED_FILES), so these
 * are sent with HTTPD_RESP_USE_STRLEN rather than (end - start): the raw
 * byte-count would include that trailing '\0' in the response body, which
 * HTML/CSS parsers silently tolerate but which is a hard syntax error to a
 * JS parser (NUL is not valid ECMAScript whitespace), breaking app.js. */

static esp_err_t handle_index(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)index_html_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_app_js(httpd_req_t *req)
{
  httpd_resp_set_type(req, "application/javascript");
  return httpd_resp_send(req, (const char *)app_js_start, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_style_css(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/css");
  return httpd_resp_send(req, (const char *)style_css_start, HTTPD_RESP_USE_STRLEN);
}

/******************************************************************************/
/*** Local function implementation — mode                                    */
/******************************************************************************/

static esp_err_t handle_get_mode(httpd_req_t *req)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "mode",
                           (s_mode == WEBPAGE_MODE_FIRST_RUN) ? "first_run" : "normal_setup");
  return send_json(req, root);
}

/******************************************************************************/
/*** Local function implementation — system: general                        */
/******************************************************************************/

static esp_err_t handle_get_general(httpd_req_t *req)
{
  uint32_t minutes = 0;
  nvs_GeneralGetRebootMinutes(&minutes);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "reboot_minutes", minutes);
  return send_json(req, root);
}

static esp_err_t handle_post_general(httpd_req_t *req)
{
  cJSON *body = NULL;
  if (!read_json_body(req, 256, &body))
  {
    return ESP_OK;
  }

  cJSON *item = cJSON_GetObjectItemCaseSensitive(body, "reboot_minutes");
  if (!cJSON_IsNumber(item) || item->valuedouble < 0)
  {
    cJSON_Delete(body);
    return bad_request(req, "invalid_argument", "reboot_minutes must be a non-negative number");
  }

  RC_t rc = nvs_GeneralSetRebootMinutes((uint32_t)item->valuedouble);
  cJSON_Delete(body);
  return (rc == RC_SUCCESS) ? send_ok(req) : server_error(req, "nvs_write_failed", NULL);
}

/******************************************************************************/
/*** Local function implementation — system: wifi                           */
/******************************************************************************/

static esp_err_t handle_get_wifi(httpd_req_t *req)
{
  char ssid[NVS_SSID_MAX_LEN]     = {0};
  char pass[NVS_PASSWORD_MAX_LEN] = {0};
  uint32_t fallback_s = 0;

  nvs_WifiGetSsid(ssid, sizeof(ssid));
  RC_t pass_rc = nvs_WifiGetPassword(pass, sizeof(pass));
  nvs_WifiGetApFallbackTimeout(&fallback_s);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "ssid", ssid);
  cJSON_AddBoolToObject(root, "has_password", (pass_rc == RC_SUCCESS) && (pass[0] != '\0'));
  cJSON_AddNumberToObject(root, "ap_fallback_timeout_s", fallback_s);
  return send_json(req, root);
}

static esp_err_t handle_post_wifi(httpd_req_t *req)
{
  cJSON *body = NULL;
  if (!read_json_body(req, 512, &body))
  {
    return ESP_OK;
  }

  cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(body, "ssid");
  if (!cJSON_IsString(ssid_item) || ssid_item->valuestring[0] == '\0')
  {
    cJSON_Delete(body);
    return bad_request(req, "invalid_argument", "ssid is required");
  }

  RC_t rc = nvs_WifiSetSsid(ssid_item->valuestring);

  cJSON *pass_item = cJSON_GetObjectItemCaseSensitive(body, "password");
  if (cJSON_IsString(pass_item))
  {
    /* Empty/omitted password means "keep existing" (never echoed back). */
    if (nvs_WifiSetPassword(pass_item->valuestring) != RC_SUCCESS)
    {
      rc = RC_ERROR;
    }
  }

  cJSON *fallback_item = cJSON_GetObjectItemCaseSensitive(body, "ap_fallback_timeout_s");
  if (cJSON_IsNumber(fallback_item))
  {
    if (nvs_WifiSetApFallbackTimeout((uint32_t)fallback_item->valuedouble) != RC_SUCCESS)
    {
      rc = RC_ERROR;
    }
  }

  cJSON_Delete(body);
  return (rc == RC_SUCCESS) ? send_ok(req) : server_error(req, "nvs_write_failed", NULL);
}

/******************************************************************************/
/*** Local function implementation — system: mqtt                           */
/******************************************************************************/

static esp_err_t handle_get_mqtt(httpd_req_t *req)
{
  char broker[NVS_BROKER_MAX_LEN]       = {0};
  char topic[NVS_TOPIC_MAX_LEN]         = {0};
  char user[NVS_MQTT_USER_MAX_LEN]      = {0};
  char pass[NVS_MQTT_PASS_MAX_LEN]      = {0};
  char client_id[NVS_CLIENT_ID_MAX_LEN] = {0};

  nvs_MqttGetBroker(broker, sizeof(broker));
  nvs_MqttGetTopic(topic, sizeof(topic));
  nvs_MqttGetUser(user, sizeof(user));
  RC_t pass_rc = nvs_MqttGetPass(pass, sizeof(pass));
  nvs_MqttGetClientId(client_id, sizeof(client_id));

  bool hb_enabled = false;
  char hb_topic[NVS_TOPIC_MAX_LEN]     = {0};
  char hb_msg[NVS_MQTT_MSG_MAX_LEN]    = {0};
  uint32_t hb_interval = 0;
  nvs_MqttGetHeartbeatEnabled(&hb_enabled);
  nvs_MqttGetHeartbeatTopic(hb_topic, sizeof(hb_topic));
  nvs_MqttGetHeartbeatMessage(hb_msg, sizeof(hb_msg));
  nvs_MqttGetHeartbeatInterval(&hb_interval);

  bool lw_enabled = false;
  char lw_topic[NVS_TOPIC_MAX_LEN]  = {0};
  char lw_msg[NVS_MQTT_MSG_MAX_LEN] = {0};
  nvs_MqttGetLastWillEnabled(&lw_enabled);
  nvs_MqttGetLastWillTopic(lw_topic, sizeof(lw_topic));
  nvs_MqttGetLastWillMessage(lw_msg, sizeof(lw_msg));

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "broker", broker);
  cJSON_AddStringToObject(root, "topic", topic);
  cJSON_AddStringToObject(root, "user", user);
  cJSON_AddBoolToObject(root, "has_password", (pass_rc == RC_SUCCESS) && (pass[0] != '\0'));
  cJSON_AddStringToObject(root, "client_id", client_id);

  cJSON *hb = cJSON_AddObjectToObject(root, "heartbeat");
  cJSON_AddBoolToObject(hb, "enabled", hb_enabled);
  cJSON_AddStringToObject(hb, "topic", hb_topic);
  cJSON_AddStringToObject(hb, "message", hb_msg);
  cJSON_AddNumberToObject(hb, "interval_s", hb_interval);

  cJSON *lw = cJSON_AddObjectToObject(root, "last_will");
  cJSON_AddBoolToObject(lw, "enabled", lw_enabled);
  cJSON_AddStringToObject(lw, "topic", lw_topic);
  cJSON_AddStringToObject(lw, "message", lw_msg);

  cJSON *fcs = cJSON_AddArrayToObject(root, "function_codes");
  for (uint8_t fc = NVS_FC_MIN; fc <= NVS_FC_MAX; fc++)
  {
    char fc_topic[NVS_TOPIC_MAX_LEN]  = {0};
    char fc_msg[NVS_MQTT_MSG_MAX_LEN] = {0};
    RC_t t_rc = nvs_MqttGetFcTopic(fc, fc_topic, sizeof(fc_topic));
    RC_t m_rc = nvs_MqttGetFcMessage(fc, fc_msg, sizeof(fc_msg));
    if (t_rc != RC_SUCCESS && m_rc != RC_SUCCESS)
    {
      continue; /* unused slot */
    }
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "fc", fc);
    cJSON_AddStringToObject(entry, "topic", fc_topic);
    cJSON_AddStringToObject(entry, "message", fc_msg);
    cJSON_AddItemToArray(fcs, entry);
  }

  return send_json(req, root);
}

static esp_err_t handle_post_mqtt(httpd_req_t *req)
{
  cJSON *body = NULL;
  if (!read_json_body(req, 16384, &body))
  {
    return ESP_OK;
  }

  RC_t rc = RC_SUCCESS;
  cJSON *item;

  if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(body, "broker")))
  {
    if (nvs_MqttSetBroker(item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
  }
  if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(body, "topic")))
  {
    if (nvs_MqttSetTopic(item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
  }
  if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(body, "user")))
  {
    if (nvs_MqttSetUser(item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
  }
  if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(body, "password")))
  {
    if (nvs_MqttSetPass(item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
  }
  if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(body, "client_id")))
  {
    if (nvs_MqttSetClientId(item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
  }

  cJSON *hb = cJSON_GetObjectItemCaseSensitive(body, "heartbeat");
  if (cJSON_IsObject(hb))
  {
    if (cJSON_IsBool(item = cJSON_GetObjectItemCaseSensitive(hb, "enabled")))
    {
      if (nvs_MqttSetHeartbeatEnabled(cJSON_IsTrue(item)) != RC_SUCCESS) { rc = RC_ERROR; }
    }
    if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(hb, "topic")))
    {
      if (nvs_MqttSetHeartbeatTopic(item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
    }
    if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(hb, "message")))
    {
      if (nvs_MqttSetHeartbeatMessage(item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
    }
    if (cJSON_IsNumber(item = cJSON_GetObjectItemCaseSensitive(hb, "interval_s")))
    {
      if (nvs_MqttSetHeartbeatInterval((uint32_t)item->valuedouble) != RC_SUCCESS) { rc = RC_ERROR; }
    }
  }

  cJSON *lw = cJSON_GetObjectItemCaseSensitive(body, "last_will");
  if (cJSON_IsObject(lw))
  {
    if (cJSON_IsBool(item = cJSON_GetObjectItemCaseSensitive(lw, "enabled")))
    {
      if (nvs_MqttSetLastWillEnabled(cJSON_IsTrue(item)) != RC_SUCCESS) { rc = RC_ERROR; }
    }
    if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(lw, "topic")))
    {
      if (nvs_MqttSetLastWillTopic(item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
    }
    if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(lw, "message")))
    {
      if (nvs_MqttSetLastWillMessage(item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
    }
  }

  cJSON *fcs = cJSON_GetObjectItemCaseSensitive(body, "function_codes");
  if (cJSON_IsArray(fcs))
  {
    cJSON *entry;
    cJSON_ArrayForEach(entry, fcs)
    {
      cJSON *fc_item = cJSON_GetObjectItemCaseSensitive(entry, "fc");
      if (!cJSON_IsNumber(fc_item))
      {
        continue;
      }
      int fc = (int)fc_item->valuedouble;
      if (fc < (int)NVS_FC_MIN || fc > (int)NVS_FC_MAX)
      {
        continue;
      }

      if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(entry, "topic")))
      {
        if (nvs_MqttSetFcTopic((uint8_t)fc, item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
      }
      if (cJSON_IsString(item = cJSON_GetObjectItemCaseSensitive(entry, "message")))
      {
        if (nvs_MqttSetFcMessage((uint8_t)fc, item->valuestring) != RC_SUCCESS) { rc = RC_ERROR; }
      }
    }
  }

  cJSON_Delete(body);
  return (rc == RC_SUCCESS) ? send_ok(req) : server_error(req, "nvs_write_failed", NULL);
}

/******************************************************************************/
/*** Local function implementation — system: reset / exit                   */
/******************************************************************************/

static esp_err_t handle_reset_start(httpd_req_t *req)
{
  fps_SetLed(FPM_LED_AWAITING_FINGER);
  return send_ok(req);
}

/**
 * @brief Reset Device verification (SWS-FPM104): one attempt per request.
 *
 * Wait for a finger, attempt a 1:N match, hold the corresponding result
 * LED (SCAN_SUCCESS/SCAN_FAILED) for RESULT_DISPLAY_MS, then either
 * perform the actual erase (admin matched) or go back to "awaiting
 * finger" so the frontend loops back for another try. Nobody presenting
 * a finger at all within the timeout aborts the whole attempt back to
 * the Setup-Mode baseline.
 *
 * There is no lift-detection step: performing a scan leaves the sensor
 * unable to reliably report a lift afterward (confirmed independently
 * via the vendor PC test tool — see io_WaitFingerPresent() in io.c), so
 * this doesn't try. The *next* attempt's own presence-wait already
 * requires a genuinely fresh touch, which is what actually enforces
 * "lift and place again" between tries.
 */
static esp_err_t handle_reset_scan(httpd_req_t *req)
{
  cJSON *root = cJSON_CreateObject();

  RC_t presence_rc = io_WaitFingerPresent(RESET_SCAN_TIMEOUT_MS);
  ESP_LOGI(TAG, "reset scan: io_WaitFingerPresent rc=%d", (int)presence_rc);
  if (presence_rc != RC_SUCCESS)
  {
    /* Nobody presented a finger at all within the window — abort rather
     * than waiting forever. */
    fps_SetLed(FPM_LED_DIAG_0);
    cJSON_AddStringToObject(root, "status", "aborted");
    cJSON_AddStringToObject(root, "message", "No finger detected in time. Reset cancelled.");
    return send_json(req, root);
  }

  /* Let the finger settle before the first capture attempt — see
   * SCAN_CAPTURE_SETTLE_MS above. */
  vTaskDelay(pdMS_TO_TICKS(SCAN_CAPTURE_SETTLE_MS));

  uint16_t id = 0;
  uint16_t score = 0;
  RC_t rc = fps_ScanStep(RESET_SCAN_TIMEOUT_MS, &id, &score);
  ESP_LOGI(TAG, "reset scan: fps_ScanStep rc=%d id=%u score=%u", (int)rc, id, score);

  if (rc == RC_TIMEOUT)
  {
    /* Presence was confirmed but the scan itself never produced a
     * definitive result — treat the same as never placing a finger. */
    fps_SetLed(FPM_LED_DIAG_0);
    cJSON_AddStringToObject(root, "status", "aborted");
    cJSON_AddStringToObject(root, "message", "No finger detected in time. Reset cancelled.");
    return send_json(req, root);
  }

  if (rc == RC_NO_MATCH)
  {
    fps_SetLed(FPM_LED_SCAN_FAILED);
    vTaskDelay(pdMS_TO_TICKS(RESULT_DISPLAY_MS));
    fps_SetLed(FPM_LED_AWAITING_FINGER);
    cJSON_AddStringToObject(root, "status", "no_match");
    return send_json(req, root);
  }

  /* rc == RC_SUCCESS: some fingerprint matched — check whether it's the admin's. */
  fpm_fingerprint_meta_t meta = {0};
  RC_t meta_rc = RC_ERROR;
  if (fps_Lock() == RC_SUCCESS)
  {
    meta_rc = fps_raw_read_meta(id, &meta);
    fps_Unlock();
  }
  ESP_LOGI(TAG, "reset scan: fps_raw_read_meta(id=%u) rc=%d uuid=%u finger_id=%u function_code=%u "
                "(expect finger_id==%u for admin)",
           id, (int)meta_rc, meta.uuid, meta.finger_id, meta.function_code,
           (unsigned)FPS_ADMIN_FINGER_ID);

  bool is_admin = (meta_rc == RC_SUCCESS) && (meta.finger_id == FPS_ADMIN_FINGER_ID);

  if (!is_admin)
  {
    fps_SetLed(FPM_LED_SCAN_FAILED);
    vTaskDelay(pdMS_TO_TICKS(RESULT_DISPLAY_MS));
    fps_SetLed(FPM_LED_AWAITING_FINGER);
    cJSON_AddStringToObject(root, "status", "not_admin");
    return send_json(req, root);
  }

  fps_SetLed(FPM_LED_SCAN_SUCCESS);
  vTaskDelay(pdMS_TO_TICKS(RESULT_DISPLAY_MS));

  /* Erase the sensor's fingerprint library first, and only proceed to
   * wipe NVS (which is what app_mode_HasAdminFingerprint() trusts, via
   * the admin-enrolled flag) if that actually succeeded. Erasing NVS
   * first — or unconditionally — could leave the admin-enrolled flag
   * cleared while an admin template is still physically present
   * (harmless), or worse, leave it cleared while erasing NVS itself
   * fails part-way (harmless too, since a partial NVS erase can't leave
   * the flag *true*). The one ordering that would be unsafe is
   * nvs_flash_erase() succeeding before fps_raw_delete_all() has been
   * confirmed, so we simply never do that. */
  RC_t erase_rc = RC_ERROR;
  if (fps_Lock() == RC_SUCCESS)
  {
    erase_rc = fps_raw_delete_all();
    fps_Unlock();
  }
  ESP_LOGI(TAG, "reset scan: fps_raw_delete_all rc=%d", (int)erase_rc);

  if (erase_rc != RC_SUCCESS)
  {
    fps_SetLed(FPM_LED_ERROR_1);
    timer_OneShot(LED_RESTORE_DELAY_MS, timer_restore_diag0_cb, NULL, NULL);
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "message", "Failed to erase the fingerprint library.");
    return send_json(req, root);
  }

  nvs_flash_erase();

  cJSON_AddStringToObject(root, "status", "success");
  esp_err_t send_rc = send_json(req, root);
  timer_OneShot(RESET_REBOOT_DELAY_MS, timer_reboot_cb, NULL, NULL);
  return send_rc;
}

static esp_err_t handle_exit(httpd_req_t *req)
{
  esp_err_t rc = send_ok(req);
  timer_OneShot(POST_REBOOT_DELAY_MS, timer_reboot_cb, NULL, NULL);
  return rc;
}

/******************************************************************************/
/*** Local function implementation — users                                   */
/******************************************************************************/

static esp_err_t handle_get_users(httpd_req_t *req)
{
  nvs_user_entry_t *entries = malloc(sizeof(nvs_user_entry_t) * NVS_USER_MAX_COUNT);
  if (entries == NULL)
  {
    return server_error(req, "out_of_memory", NULL);
  }

  size_t count = 0;
  nvs_UserList(entries, NVS_USER_MAX_COUNT, &count);

  cJSON *root = cJSON_CreateObject();
  cJSON *arr = cJSON_AddArrayToObject(root, "users");
  for (size_t i = 0; i < count; i++)
  {
    cJSON *u = cJSON_CreateObject();
    cJSON_AddNumberToObject(u, "uuid", entries[i].uuid);
    cJSON_AddStringToObject(u, "name", entries[i].name);
    cJSON_AddItemToArray(arr, u);
  }
  free(entries);

  return send_json(req, root);
}

static esp_err_t handle_post_user(httpd_req_t *req)
{
  unsigned uuid = 0;
  if (!get_query_uint(req, "uuid", &uuid) || uuid < 1U || uuid > 127U)
  {
    return bad_request(req, "invalid_argument", "uuid query param required, 1-127");
  }

  cJSON *body = NULL;
  if (!read_json_body(req, 128, &body))
  {
    return ESP_OK;
  }

  cJSON *name = cJSON_GetObjectItemCaseSensitive(body, "name");
  if (!cJSON_IsString(name) || name->valuestring[0] == '\0')
  {
    cJSON_Delete(body);
    return bad_request(req, "invalid_argument", "name is required");
  }

  RC_t rc = nvs_UserSetName((uint8_t)uuid, name->valuestring);
  cJSON_Delete(body);
  return (rc == RC_SUCCESS) ? send_ok(req) : server_error(req, "nvs_write_failed", NULL);
}

static esp_err_t handle_delete_user(httpd_req_t *req)
{
  unsigned uuid = 0;
  if (!get_query_uint(req, "uuid", &uuid) || uuid < 1U || uuid > 127U)
  {
    return bad_request(req, "invalid_argument", "uuid query param required, 1-127");
  }

  nvs_UserDelete((uint8_t)uuid);
  if (fps_Lock() == RC_SUCCESS)
  {
    fps_raw_delete_by_uuid((uint8_t)uuid);
    fps_Unlock();
  }
  return send_ok(req);
}

/******************************************************************************/
/*** Local function implementation — fingerprints                            */
/******************************************************************************/

static esp_err_t handle_get_fingerprints(httpd_req_t *req)
{
  unsigned filter_uuid = 0;
  bool has_filter = get_query_uint(req, "uuid", &filter_uuid) && filter_uuid <= 127U;

  fpm_fingerprint_entry_t *entries = malloc(sizeof(fpm_fingerprint_entry_t) * FPS_MAX_TEMPLATES);
  if (entries == NULL)
  {
    return server_error(req, "out_of_memory", NULL);
  }

  uint16_t count = 0;
  RC_t rc = RC_TIMEOUT;
  if (fps_Lock() == RC_SUCCESS)
  {
    rc = fps_raw_list(entries, FPS_MAX_TEMPLATES, &count);
    fps_Unlock();
  }
  if (rc != RC_SUCCESS)
  {
    free(entries);
    return server_error(req, "sensor_error", NULL);
  }

  cJSON *root = cJSON_CreateObject();
  cJSON *arr = cJSON_AddArrayToObject(root, "fingerprints");
  uint16_t relative_index = 0;

  for (uint16_t i = 0; i < count; i++)
  {
    const fpm_fingerprint_meta_t *meta = &entries[i].meta;

    /* The admin/master fingerprint is never shown in the Fingerprint
     * Library — it isn't a regular user's fingerprint, and showing it
     * next to a "Delete" button that handle_delete_fingerprint() would
     * refuse anyway is just confusing. It's still protected there in
     * depth (e.g. against a crafted request), this just keeps it out of
     * the UI entirely. */
    if (meta->finger_id == FPS_ADMIN_FINGER_ID)
    {
      continue;
    }

    if (has_filter && meta->uuid != filter_uuid)
    {
      continue;
    }

    char name[NVS_USER_NAME_MAX_LEN] = {0};
    nvs_UserGetName(meta->uuid, name, sizeof(name));

    cJSON *fp = cJSON_CreateObject();
    cJSON_AddNumberToObject(fp, "id", entries[i].id);
    cJSON_AddNumberToObject(fp, "uuid", meta->uuid);
    cJSON_AddStringToObject(fp, "name", name);
    cJSON_AddNumberToObject(fp, "finger_id", meta->finger_id);
    cJSON_AddNumberToObject(fp, "function_code", meta->function_code);
    if (has_filter)
    {
      relative_index++;
      cJSON_AddNumberToObject(fp, "relative_index", relative_index);
    }
    cJSON_AddItemToArray(arr, fp);
  }
  free(entries);

  return send_json(req, root);
}

static esp_err_t handle_delete_fingerprint(httpd_req_t *req)
{
  unsigned id = 0;
  if (!get_query_uint(req, "id", &id))
  {
    return bad_request(req, "invalid_argument", "id query param required");
  }

  if (fps_Lock() != RC_SUCCESS)
  {
    return server_error(req, "sensor_busy", NULL);
  }

  /* Refuse to delete the admin's own template(s) through this generic
   * endpoint: it would desync nvs_GeneralGetAdminEnrolled() (still true)
   * from reality (no admin template left to scan/match), locking the
   * device out of Reset Device with no way to recover it in software.
   * Removing the admin fingerprint is only possible via the full
   * factory-reset flow (handle_reset_scan), which clears the flag too. */
  fpm_fingerprint_meta_t meta = {0};
  if (fps_raw_read_meta((uint16_t)id, &meta) == RC_SUCCESS &&
      meta.finger_id == FPS_ADMIN_FINGER_ID)
  {
    fps_Unlock();
    return bad_request(req, "cannot_delete_admin",
                        "the admin fingerprint can only be removed via Reset Device");
  }

  RC_t rc = fps_raw_delete((uint16_t)id);
  fps_Unlock();

  return (rc == RC_SUCCESS) ? send_ok(req) : server_error(req, "sensor_error", NULL);
}

/******************************************************************************/
/*** Local function implementation — enroll wizard                           */
/******************************************************************************/

/**
 * @brief Drive one step of the 3-template x 2-scan enrollment wizard
 *        (SWS-WP104.3 / SWS-WP201).
 *
 * Shared by the regular add-fingerprint flow and First-Run-Mode admin
 * enrollment — only the target (uuid, finger_id, function_code) and
 * whether completing template 3 should reboot into (now Normal-)
 * Setup-Mode differ.
 */
static esp_err_t do_enroll_scan(httpd_req_t *req, uint8_t uuid, uint8_t finger_id,
                                 uint8_t function_code, bool is_first_run)
{
  cJSON *body = NULL;
  if (!read_json_body(req, 64, &body))
  {
    return ESP_OK;
  }

  cJSON *template_item = cJSON_GetObjectItemCaseSensitive(body, "template");
  cJSON *step_item     = cJSON_GetObjectItemCaseSensitive(body, "step");
  if (!cJSON_IsNumber(template_item) || !cJSON_IsNumber(step_item))
  {
    cJSON_Delete(body);
    return bad_request(req, "invalid_argument", "template (1-3) and step (1-2) are required");
  }
  int template_num = (int)template_item->valuedouble;
  int step         = (int)step_item->valuedouble;
  cJSON_Delete(body);

  if (template_num < 1 || template_num > 3 || (step != 1 && step != 2))
  {
    return bad_request(req, "invalid_argument", "template must be 1-3, step must be 1-2");
  }

  cJSON *root = cJSON_CreateObject();
  bool is_final_capture = (template_num == 3) && (step == 2);

  if (template_num == 1 && step == 1)
  {
    s_pending_enroll.committed_count = 0; /* a fresh attempt is starting */
  }

  /* Idempotent (fps_SetLed no-ops if already in this state) — safe to call
   * unconditionally on every request, including retries of the same step,
   * without restarting/glitching the breathing animation. */
  fps_SetLed(FPM_LED_AWAITING_FINGER);

  RC_t presence_rc = io_WaitFingerPresent(ENROLL_STEP_TIMEOUT_MS);
  if (presence_rc != RC_SUCCESS)
  {
    /* Just the internal poll window elapsing while genuinely waiting for a
     * finger to be placed — not an error, so no LED change and no abort
     * here: it's still showing AWAITING_FINGER and should stay that way.
     * The frontend simply calls this endpoint again; the user's own escape
     * hatch out of a stalled wait is the wizard's Cancel control, not an
     * automatic timeout (see SWS-FPM103). */
    cJSON_AddStringToObject(root, "status", "timeout");
    return send_json(req, root);
  }

  /* A finger is confirmed present — show a genuine "scanning" state for
   * the actual capture window (see SCANNING_DISPLAY_MIN_MS above), rather
   * than staying on "awaiting finger" right up until the result appears. */
  fps_SetLed(FPM_LED_SCANNING);
  TickType_t scan_start = xTaskGetTickCount();

  RC_t rc = fps_EnrollStep((uint8_t)step, ENROLL_STEP_TIMEOUT_MS);

  TickType_t scan_elapsed = xTaskGetTickCount() - scan_start;
  TickType_t scan_min_ticks = pdMS_TO_TICKS(SCANNING_DISPLAY_MIN_MS);
  if (scan_elapsed < scan_min_ticks)
  {
    vTaskDelay(scan_min_ticks - scan_elapsed);
  }

  if (rc != RC_SUCCESS)
  {
    /* The capture itself never produced a definitive result within
     * budget — routine, not an error; go back to "awaiting finger" so the
     * next request's presence-wait correctly requires a fresh touch. */
    fps_SetLed(FPM_LED_AWAITING_FINGER);
    cJSON_AddStringToObject(root, "status", "timeout");
    return send_json(req, root);
  }

  if (step == 1)
  {
    /* No lift-detection step between scans (see RESULT_DISPLAY_MS above):
     * hold the result LED, then go straight back to "awaiting finger" —
     * the next capture's own io_WaitFingerPresent() requires a genuinely
     * fresh touch, which is what actually enforces "lift and place
     * again" for the frontend's next instruction. */
    led_result_then_awaiting();

    cJSON_AddStringToObject(root, "status", "success");
    cJSON_AddNumberToObject(root, "template", template_num);
    cJSON_AddNumberToObject(root, "step", 1);
    return send_json(req, root);
  }

  /* step == 2: commit + tag */
  fpm_fingerprint_meta_t meta = {
    .function_code = function_code,
    .finger_id     = finger_id,
    .uuid          = uuid,
  };
  uint16_t id = 0;
  rc = fps_EnrollCommitAndTag(&meta, &id);
  if (rc != RC_SUCCESS)
  {
    /* A real failure, not routine waiting — abort the whole attempt and
     * discard whatever templates it already stored. */
    discard_session_templates();
    s_pending_enroll.armed = false;
    fps_SetLed(FPM_LED_ERROR_1);
    timer_OneShot(LED_RESTORE_DELAY_MS, timer_restore_diag0_cb, NULL, NULL);
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "message", "Failed to store fingerprint template.");
    return send_json(req, root);
  }

  if (s_pending_enroll.committed_count < 3U)
  {
    s_pending_enroll.committed_ids[s_pending_enroll.committed_count++] = id;
  }

  cJSON_AddStringToObject(root, "status", "success");
  cJSON_AddNumberToObject(root, "id", id);
  cJSON_AddNumberToObject(root, "template", template_num);

  if (is_final_capture)
  {
    /* Just at the end, once the whole enrollment has succeeded: op_success.
     * Nothing more to place, so no result-then-awaiting transition follows
     * this one. Forget the tracked ids without deleting them — they're
     * being kept. */
    s_pending_enroll.committed_count = 0;
    fps_SetLed(FPM_LED_OP_SUCCESS);

    if (is_first_run)
    {
      /* SWS-MOD108: persist that an admin fingerprint now exists — this
       * flag, not a sensor scan, is what app_mode_HasAdminFingerprint()
       * checks. */
      nvs_GeneralSetAdminEnrolled(true);
      /* SWS-WP201: re-arm the setup flag so the next boot lands back in
       * (now Normal-) Setup-Mode to let the admin configure wifi/mqtt. */
      nvs_GeneralSetSetupFlag(true);
      cJSON_AddBoolToObject(root, "rebooting", true);
      esp_err_t send_rc = send_json(req, root);
      timer_OneShot(ENROLL_REBOOT_DELAY_MS, timer_reboot_cb, NULL, NULL);
      return send_rc;
    }

    timer_OneShot(LED_RESTORE_DELAY_MS, timer_restore_diag0_cb, NULL, NULL);
    s_pending_enroll.armed = false; /* wizard complete */
    return send_json(req, root);
  }

  /* Not the final capture overall (template 1 or 2's 2nd scan) — hold the
   * result LED, then go straight back to "awaiting finger" for the next
   * template's first capture (see RESULT_DISPLAY_MS above). */
  led_result_then_awaiting();

  return send_json(req, root);
}

static esp_err_t handle_enroll_prepare(httpd_req_t *req)
{
  cJSON *body = NULL;
  if (!read_json_body(req, 256, &body))
  {
    return ESP_OK;
  }

  cJSON *uuid_item   = cJSON_GetObjectItemCaseSensitive(body, "uuid");
  cJSON *finger_item = cJSON_GetObjectItemCaseSensitive(body, "finger_id");
  cJSON *fc_item     = cJSON_GetObjectItemCaseSensitive(body, "function_code");

  if (!cJSON_IsNumber(uuid_item) || !cJSON_IsNumber(finger_item) || !cJSON_IsNumber(fc_item))
  {
    cJSON_Delete(body);
    return bad_request(req, "invalid_argument",
                        "uuid, finger_id, function_code are required numbers");
  }

  int uuid      = (int)uuid_item->valuedouble;
  int finger_id = (int)finger_item->valuedouble;
  int fc        = (int)fc_item->valuedouble;
  cJSON_Delete(body);

  if (uuid < 1 || uuid > 127)
  {
    return bad_request(req, "invalid_argument", "uuid must be 1-127");
  }
  if (finger_id < 0 || finger_id > 9)
  {
    return bad_request(req, "invalid_finger_id", "finger_id must be 0-9");
  }
  if (fc < (int)NVS_FC_MIN || fc > (int)NVS_FC_MAX)
  {
    return bad_request(req, "invalid_function_code", "function_code must be 1-31");
  }

  char name[NVS_USER_NAME_MAX_LEN] = {0};
  if (nvs_UserGetName((uint8_t)uuid, name, sizeof(name)) != RC_SUCCESS)
  {
    return bad_request(req, "uuid_not_found", NULL);
  }

  s_pending_enroll.armed         = true;
  s_pending_enroll.uuid          = (uint8_t)uuid;
  s_pending_enroll.finger_id     = (uint8_t)finger_id;
  s_pending_enroll.function_code = (uint8_t)fc;

  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddNumberToObject(root, "template_count", 3);
  return send_json(req, root);
}

static esp_err_t handle_enroll_scan(httpd_req_t *req)
{
  if (!s_pending_enroll.armed)
  {
    return bad_request(req, "not_prepared", "call /api/fingerprints/enroll/prepare first");
  }
  return do_enroll_scan(req, s_pending_enroll.uuid, s_pending_enroll.finger_id,
                         s_pending_enroll.function_code, false);
}

static esp_err_t handle_enroll_cancel(httpd_req_t *req)
{
  discard_session_templates();
  s_pending_enroll.armed = false;
  fps_SetLed(FPM_LED_DIAG_0);
  return send_ok(req);
}

static esp_err_t handle_firstrun_scan(httpd_req_t *req)
{
  return do_enroll_scan(req, FPS_ADMIN_UUID, FPS_ADMIN_FINGER_ID, FPS_ADMIN_FUNCTION_CODE, true);
}
