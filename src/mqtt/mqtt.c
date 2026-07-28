/**
 * @file       mqtt.c
 * @brief      MQTT client
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "mqtt/mqtt.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "app_handles.h"
#include "wdt/wdt.h"
#include "ceh/ceh.h"
#include "nvs/nvs_app.h"
#include "timer/timer.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/** NVS only stores a bare broker host/IP, no port — plain MQTT on the
 *  standard port (no TLS in this project's current scope). */
#define MQTT_BROKER_PORT_DEFAULT           1883U
#define MQTT_URI_MAX_LEN                   (NVS_BROKER_MAX_LEN + 16U)

/** Overall budget for mqtt_Init()'s blocking connect (SWS-MOT01). Generous
 *  on purpose: esp-mqtt's own internal TCP-connect timeout is ~10s, and a
 *  single failed attempt (broker briefly unreachable, slow DNS/routing on
 *  the local network, etc.) is retried automatically in the background —
 *  MQTT_EVENT_ERROR is not treated as fatal (see mqtt_EventHandler below),
 *  so this budget needs to cover several such retries, not just one. */
#define MQTT_INIT_CONNECT_TIMEOUT_MS       60000U
#define MQTT_RUNTIME_DISCONNECT_TIMEOUT_MS 120000U
#define MQTT_TASK_WDT_TIMEOUT_MS           5000U

/** Working buffer size for placeholder substitution — comfortably larger
 *  than the longest NVS template (NVS_MQTT_MSG_MAX_LEN) plus expansions. */
#define MQTT_SUBST_BUF_LEN                 256U

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client      = NULL;
static SemaphoreHandle_t        s_connect_sem = NULL;
static volatile bool            s_connect_ok  = false;

static bool     s_hb_enabled     = false;
static uint32_t s_hb_interval_s  = 0U;
static char     s_hb_topic[NVS_TOPIC_MAX_LEN];
static char     s_hb_message[NVS_MQTT_MSG_MAX_LEN];
static timer_handle_t s_heartbeat_handle = NULL;

static timer_handle_t s_runtime_disconnect_handle = NULL;

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static void mqtt_EventHandler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data);
static void mqtt_HeartbeatCb(void *i_arg);
static void mqtt_RuntimeDisconnectTimeoutCb(void *i_arg);

static void mqtt_PublishScanEvent(const mqtt_scan_event_t *i_event);
static void substitute_placeholders(const char *i_template, char *o_buf, size_t i_buf_len,
                                     const mqtt_scan_event_t *i_event);
static void replace_token(const char *i_in, char *o_out, size_t i_out_len,
                          const char *i_token, const char *i_value);

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t mqtt_Init(void)
{
  char broker[NVS_BROKER_MAX_LEN]       = {0};
  char user[NVS_MQTT_USER_MAX_LEN]      = {0};
  char pass[NVS_MQTT_PASS_MAX_LEN]      = {0};
  char client_id[NVS_CLIENT_ID_MAX_LEN] = {0};

  nvs_MqttGetBroker(broker, sizeof(broker));
  nvs_MqttGetUser(user, sizeof(user));
  nvs_MqttGetPass(pass, sizeof(pass));
  nvs_MqttGetClientId(client_id, sizeof(client_id));

  if (broker[0] == '\0')
  {
    ESP_LOGE(TAG, "no MQTT broker configured");
    return RC_ERROR;
  }

  char uri[MQTT_URI_MAX_LEN];
  snprintf(uri, sizeof(uri), "mqtt://%s:%u", broker, MQTT_BROKER_PORT_DEFAULT);

  bool lw_enabled = false;
  char lw_topic[NVS_TOPIC_MAX_LEN]      = {0};
  char lw_message[NVS_MQTT_MSG_MAX_LEN] = {0};
  nvs_MqttGetLastWillEnabled(&lw_enabled);
  if (lw_enabled)
  {
    nvs_MqttGetLastWillTopic(lw_topic, sizeof(lw_topic));
    nvs_MqttGetLastWillMessage(lw_message, sizeof(lw_message));
  }

  nvs_MqttGetHeartbeatEnabled(&s_hb_enabled);
  nvs_MqttGetHeartbeatTopic(s_hb_topic, sizeof(s_hb_topic));
  nvs_MqttGetHeartbeatMessage(s_hb_message, sizeof(s_hb_message));
  nvs_MqttGetHeartbeatInterval(&s_hb_interval_s);

  esp_mqtt_client_config_t cfg = {0};
  cfg.broker.address.uri = uri;
  cfg.credentials.client_id = (client_id[0] != '\0') ? client_id : NULL;
  cfg.credentials.username  = (user[0] != '\0') ? user : NULL;
  if (pass[0] != '\0')
  {
    cfg.credentials.authentication.password = pass;
  }
  if (lw_enabled)
  {
    cfg.session.last_will.topic  = lw_topic;
    cfg.session.last_will.msg    = lw_message;
    cfg.session.last_will.qos    = 1;
    cfg.session.last_will.retain = 0;
  }

  s_client = esp_mqtt_client_init(&cfg);
  if (s_client == NULL)
  {
    ESP_LOGE(TAG, "esp_mqtt_client_init failed");
    return RC_ERROR;
  }

  if (s_connect_sem == NULL)
  {
    s_connect_sem = xSemaphoreCreateBinary();
    if (s_connect_sem == NULL)
    {
      ESP_LOGE(TAG, "xSemaphoreCreateBinary failed");
      return RC_ERROR;
    }
  }
  s_connect_ok = false;

  esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_EventHandler, NULL);

  if (esp_mqtt_client_start(s_client) != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_mqtt_client_start failed");
    return RC_ERROR;
  }

  if (xSemaphoreTake(s_connect_sem, pdMS_TO_TICKS(MQTT_INIT_CONNECT_TIMEOUT_MS)) != pdTRUE)
  {
    ESP_LOGE(TAG, "MQTT connect timed out");
    return RC_TIMEOUT;
  }

  if (!s_connect_ok)
  {
    ESP_LOGE(TAG, "MQTT connect failed");
    return RC_ERROR;
  }

  if (s_hb_enabled && (s_hb_interval_s > 0U))
  {
    timer_OneShot(s_hb_interval_s * 1000U, mqtt_HeartbeatCb, NULL, &s_heartbeat_handle);
  }

  ESP_LOGI(TAG, "connected to broker");
  return RC_SUCCESS;
}

void mqtt_Task(void *pvParam)
{
  (void)pvParam;
  wdt_RegisterTask(MQTT_TASK_WDT_TIMEOUT_MS);

  for (;;)
  {
    /* SWS-MOD203 allows up to 2 minutes of WiFi/MQTT reconnection before
       that's treated as fatal — an indefinite portMAX_DELAY wait here
       would starve wdt_Reset() for that whole window and trip the much
       shorter MQTT_TASK_WDT_TIMEOUT_MS well before the legitimate 2-minute
       grace period elapses. Wait in short bounded chunks and pet the
       watchdog between them instead. */
    while ((xEventGroupWaitBits(g_sys_events, EVT_WIFI_CONNECTED | EVT_MQTT_CONNECTED,
                                pdFALSE, pdTRUE, pdMS_TO_TICKS(1000))
            & (EVT_WIFI_CONNECTED | EVT_MQTT_CONNECTED))
           != (EVT_WIFI_CONNECTED | EVT_MQTT_CONNECTED))
    {
      wdt_Reset();
    }

    mqtt_scan_event_t event;
    if (xQueueReceive(g_scan_queue, &event, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
      if (event.matched)
      {
        mqtt_PublishScanEvent(&event);
      }
      /* SWS-MQT04: a no-match scan produces no publish at all. */
    }

    wdt_Reset();
  }
}

/******************************************************************************/
/*** Local function implementation                                            */
/******************************************************************************/

static void mqtt_EventHandler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
  (void)handler_args;
  (void)base;
  (void)event_data;

  switch ((esp_mqtt_event_id_t)event_id)
  {
    case MQTT_EVENT_CONNECTED:
      xEventGroupSetBits(g_sys_events, EVT_MQTT_CONNECTED);
      ceh_ClearCondition(CEH_ERR_MQTT_RUNTIME);
      timer_Cancel(s_runtime_disconnect_handle);
      s_runtime_disconnect_handle = NULL;
      s_connect_ok = true;
      xSemaphoreGive(s_connect_sem);
      break;

    case MQTT_EVENT_DISCONNECTED:
    {
      EventBits_t bits = xEventGroupGetBits(g_sys_events);
      if ((bits & EVT_MQTT_CONNECTED) != 0U)
      {
        /* SWS-MOD203: was connected, now dropped — retry for up to 2
           minutes before escalating to a fatal reboot. A disconnect during
           the initial connect attempt (mqtt_Init still waiting) does not
           hit this branch, since EVT_MQTT_CONNECTED was never set yet. */
        xEventGroupClearBits(g_sys_events, EVT_MQTT_CONNECTED);
        ESP_LOGW(TAG, "MQTT disconnected after prior connect; reconnecting");
        ceh_NonFatal(CEH_ERR_MQTT_RUNTIME, "mqtt disconnected, reconnecting");
        timer_OneShot(MQTT_RUNTIME_DISCONNECT_TIMEOUT_MS, mqtt_RuntimeDisconnectTimeoutCb,
                      NULL, &s_runtime_disconnect_handle);
      }
      break;
    }

    case MQTT_EVENT_ERROR:
      /* Not treated as a final failure: esp-mqtt retries the connection
         automatically in the background on its own schedule. Giving up
         (and unblocking mqtt_Init()) on the very first transient error —
         a slow/briefly-unreachable broker, DNS hiccup, etc. — would
         defeat the point of MQTT_INIT_CONNECT_TIMEOUT_MS's larger budget.
         mqtt_Init()'s own bounded wait (below) is what actually gives up,
         once that whole budget elapses with no successful connect. */
      ESP_LOGW(TAG, "MQTT connect attempt failed, retrying");
      break;

    default:
      break;
  }
}

static void mqtt_HeartbeatCb(void *i_arg)
{
  (void)i_arg;

  if ((xEventGroupGetBits(g_sys_events) & EVT_MQTT_CONNECTED) != 0U)
  {
    esp_mqtt_client_publish(s_client, s_hb_topic, s_hb_message, 0, 1, 0);
  }

  /* Self-re-arming: timer.h only offers one-shot timers. */
  timer_OneShot(s_hb_interval_s * 1000U, mqtt_HeartbeatCb, NULL, &s_heartbeat_handle);
}

static void mqtt_RuntimeDisconnectTimeoutCb(void *i_arg)
{
  (void)i_arg;
  ceh_Fatal(CEH_ERR_MQTT_RUNTIME);
}

/**
 * @brief SWS-MQT04: publish a matched scan event via its function code's
 * NVS (topic, message) pair, with placeholders substituted.
 */
static void mqtt_PublishScanEvent(const mqtt_scan_event_t *i_event)
{
  if ((i_event->function_code < NVS_FC_MIN) || (i_event->function_code > NVS_FC_MAX))
  {
    ESP_LOGW(TAG, "match with out-of-range function_code %u, skipping publish",
             (unsigned)i_event->function_code);
    return;
  }

  char topic_tpl[NVS_TOPIC_MAX_LEN]   = {0};
  char msg_tpl[NVS_MQTT_MSG_MAX_LEN]  = {0};

  RC_t t_rc = nvs_MqttGetFcTopic(i_event->function_code, topic_tpl, sizeof(topic_tpl));
  RC_t m_rc = nvs_MqttGetFcMessage(i_event->function_code, msg_tpl, sizeof(msg_tpl));

  if ((t_rc != RC_SUCCESS) && (m_rc != RC_SUCCESS))
  {
    ESP_LOGW(TAG, "function_code %u has no configured topic/message, skipping publish",
             (unsigned)i_event->function_code);
    return;
  }

  char topic[MQTT_SUBST_BUF_LEN];
  char message[MQTT_SUBST_BUF_LEN];
  substitute_placeholders(topic_tpl, topic, sizeof(topic), i_event);
  substitute_placeholders(msg_tpl, message, sizeof(message), i_event);

  esp_mqtt_client_publish(s_client, topic, message, 0, 1, 0);
}

/**
 * @brief Replace {uuid}/{finger_id}/{function_code}/{score}/{name} in
 * i_template with i_event's actual values (SWS-MQT04). Plain sequential
 * find-and-replace — no templating engine needed.
 */
static void substitute_placeholders(const char *i_template, char *o_buf, size_t i_buf_len,
                                     const mqtt_scan_event_t *i_event)
{
  char uuid_s[8], finger_s[8], fc_s[8], score_s[8];
  snprintf(uuid_s, sizeof(uuid_s), "%u", (unsigned)i_event->uuid);
  snprintf(finger_s, sizeof(finger_s), "%u", (unsigned)i_event->finger_id);
  snprintf(fc_s, sizeof(fc_s), "%u", (unsigned)i_event->function_code);
  snprintf(score_s, sizeof(score_s), "%u", (unsigned)i_event->score);

  char buf_a[MQTT_SUBST_BUF_LEN];
  char buf_b[MQTT_SUBST_BUF_LEN];

  replace_token(i_template, buf_a, sizeof(buf_a), "{uuid}", uuid_s);
  replace_token(buf_a, buf_b, sizeof(buf_b), "{finger_id}", finger_s);
  replace_token(buf_b, buf_a, sizeof(buf_a), "{function_code}", fc_s);
  replace_token(buf_a, buf_b, sizeof(buf_b), "{score}", score_s);
  replace_token(buf_b, buf_a, sizeof(buf_a), "{name}", i_event->name);

  strlcpy(o_buf, buf_a, i_buf_len);
}

/** Copies i_in to o_out with every occurrence of i_token replaced by
 *  i_value, truncating (never overflowing) at i_out_len. */
static void replace_token(const char *i_in, char *o_out, size_t i_out_len,
                          const char *i_token, const char *i_value)
{
  size_t token_len = strlen(i_token);
  size_t value_len  = strlen(i_value);
  size_t out_pos    = 0U;
  const char *p     = i_in;

  while ((*p != '\0') && (out_pos + 1U < i_out_len))
  {
    if (strncmp(p, i_token, token_len) == 0)
    {
      size_t remaining = i_out_len - out_pos - 1U;
      size_t copy_len  = (value_len < remaining) ? value_len : remaining;
      memcpy(&o_out[out_pos], i_value, copy_len);
      out_pos += copy_len;
      p       += token_len;
    }
    else
    {
      o_out[out_pos++] = *p++;
    }
  }
  o_out[out_pos] = '\0';
}
