/**
 * @file       wifi.c
 * @brief      WiFi connectivity management
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
#include "wifi/wifi.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "app_handles.h"
#include "wdt/wdt.h"
#include "timer/timer.h"
#include "ceh/ceh.h"
#include "nvs/nvs_app.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

#define WIFI_AP_SSID      "Esp32FingerprintScanner"
#define WIFI_AP_PASSWORD  "esp32setup"
#define WIFI_AP_CHANNEL   1U
#define WIFI_AP_MAX_CONN  4U

/** wifi_Task's own loop is a tight 1s vTaskDelay; a small multiple of that
 *  is enough headroom for the software watchdog. */
#define WIFI_TASK_WDT_TIMEOUT_MS  5000U

/** SWS-MOD203: how long a runtime WiFi drop (post-boot) is given to
 *  reconnect before escalating to a fatal reboot. */
#define WIFI_RUNTIME_DISCONNECT_TIMEOUT_MS  120000U

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

/* Not "wifi" — that tag belongs to ESP-IDF's own WiFi driver, which is very
 * chatty at INFO level (connection state machine, power-save transitions,
 * etc.). Keeping this distinct lets wifi_Init() quiet the driver's tag
 * down to WARN without silencing this module's own status logs too. */
static const char *TAG = "wifi_mgmt";

static wifi_boot_mode_t s_boot_mode = WIFI_BOOT_AP;  /* fail-safe default */

static timer_handle_t s_connect_timeout_handle     = NULL;
static timer_handle_t s_ap_fallback_handle         = NULL;
static timer_handle_t s_runtime_disconnect_handle  = NULL;

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static void start_ap_mode(void);
static void start_sta_mode(const char *i_ssid, const char *i_pass);

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data);

static void connect_timeout_cb(void *i_arg);
static void ap_fallback_cb(void *i_arg);
static void runtime_disconnect_timeout_cb(void *i_arg);

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t wifi_Init(void)
{
  /* Quiet ESP-IDF's own WiFi driver ("wifi" tag) down to warnings-and-up —
   * at INFO it logs its connection state machine, power-save transitions,
   * mode/channel changes etc. on practically every event, drowning out
   * this module's own handful of status logs (kept at TAG "wifi_mgmt",
   * unaffected by this call). */
  esp_log_level_set("wifi", ESP_LOG_WARN);

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    ESP_LOGE(TAG, "esp_netif_init failed: 0x%x", err);
    return RC_ERROR;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    ESP_LOGE(TAG, "esp_event_loop_create_default failed: 0x%x", err);
    return RC_ERROR;
  }

  if (esp_netif_create_default_wifi_sta() == NULL ||
      esp_netif_create_default_wifi_ap()  == NULL)
  {
    ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta/ap failed");
    return RC_ERROR;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_wifi_init failed");
    return RC_ERROR;
  }

  if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifi_event_handler, NULL, NULL) != ESP_OK)
  {
    ESP_LOGE(TAG, "register WIFI_EVENT handler failed");
    return RC_ERROR;
  }

  if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
        &ip_event_handler, NULL, NULL) != ESP_OK)
  {
    ESP_LOGE(TAG, "register IP_EVENT_STA_GOT_IP handler failed");
    return RC_ERROR;
  }

  /* Setup-Mode's third boot-LED stage (SWS-MOD109) needs to know once a
   * client has actually associated with our own AP and been handed an
   * address by the DHCP server, not just that the AP is up. */
  if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ASSIGNED_IP_TO_CLIENT,
        &ip_event_handler, NULL, NULL) != ESP_OK)
  {
    ESP_LOGE(TAG, "register IP_EVENT_ASSIGNED_IP_TO_CLIENT handler failed");
    return RC_ERROR;
  }

  ESP_LOGI(TAG, "initialised");
  return RC_SUCCESS;
}

void wifi_SetBootMode(wifi_boot_mode_t i_mode)
{
  s_boot_mode = i_mode;
}

void wifi_Task(void *pvParam)
{
  (void)pvParam;
  wdt_RegisterTask(WIFI_TASK_WDT_TIMEOUT_MS);

  if (s_boot_mode == WIFI_BOOT_AP)
  {
    ESP_LOGI(TAG, "boot mode AP, starting AP mode");
    start_ap_mode();
  }
  else
  {
    char ssid[NVS_SSID_MAX_LEN]     = {0};
    char pass[NVS_PASSWORD_MAX_LEN] = {0};

    RC_t ssid_rc = nvs_WifiGetSsid(ssid, sizeof(ssid));
    (void)nvs_WifiGetPassword(pass, sizeof(pass));

    if (ssid_rc != RC_SUCCESS || ssid[0] == '\0')
    {
      /* Caller requested STA mode but no credentials are stored; fall back
         to AP rather than attempting to connect with an empty SSID. */
      ESP_LOGW(TAG, "boot mode STA requested but no SSID in NVS, starting AP mode");
      start_ap_mode();
    }
    else
    {
      ESP_LOGI(TAG, "boot mode STA, connecting to '%s'", ssid);
      start_sta_mode(ssid, pass);
    }
  }

  for (;;)
  {
    wdt_Reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

RC_t wifi_WaitConnected(uint32_t i_timeout_ms)
{
  EventBits_t bits = xEventGroupWaitBits(g_sys_events, EVT_WIFI_CONNECTED, pdFALSE, pdTRUE,
                                         pdMS_TO_TICKS(i_timeout_ms));
  return ((bits & EVT_WIFI_CONNECTED) != 0U) ? RC_SUCCESS : RC_TIMEOUT;
}

/******************************************************************************/
/*** Local function implementation                                            */
/******************************************************************************/

static void start_ap_mode(void)
{
  wifi_config_t ap_cfg = {0};
  strlcpy((char *)ap_cfg.ap.ssid, WIFI_AP_SSID, sizeof(ap_cfg.ap.ssid));
  ap_cfg.ap.ssid_len = strlen(WIFI_AP_SSID);
  strlcpy((char *)ap_cfg.ap.password, WIFI_AP_PASSWORD, sizeof(ap_cfg.ap.password));
  ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
  ap_cfg.ap.channel        = WIFI_AP_CHANNEL;
  ap_cfg.ap.max_connection = WIFI_AP_MAX_CONN;

  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
  if (err == ESP_OK) err = esp_wifi_start();

  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "start_ap_mode failed: 0x%x", err);
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  xEventGroupSetBits(g_sys_events, EVT_WIFI_AP_MODE);
  ESP_LOGI(TAG, "AP mode started: SSID=%s", WIFI_AP_SSID);
}

static void start_sta_mode(const char *i_ssid, const char *i_pass)
{
  wifi_config_t sta_cfg = {0};
  strlcpy((char *)sta_cfg.sta.ssid, i_ssid, sizeof(sta_cfg.sta.ssid));
  strlcpy((char *)sta_cfg.sta.password, i_pass, sizeof(sta_cfg.sta.password));

  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  if (err == ESP_OK) err = esp_wifi_start();

  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "start_sta_mode failed: 0x%x", err);
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  /* esp_wifi_connect() is issued from the WIFI_EVENT_STA_START handler once
     the driver reports it is actually ready; calling it here would race
     esp_wifi_start()'s asynchronous bring-up. */

  if (timer_OneShot(WIFI_STA_CONNECT_TIMEOUT_MS, connect_timeout_cb, NULL,
                    &s_connect_timeout_handle) != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "failed to arm connect-timeout timer");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
  (void)arg;
  (void)base;
  (void)event_data;

  switch (event_id)
  {
    case WIFI_EVENT_STA_START:
      esp_wifi_connect();
      break;

    case WIFI_EVENT_STA_DISCONNECTED:
    {
      EventBits_t bits = xEventGroupGetBits(g_sys_events);
      if ((bits & EVT_WIFI_CONNECTED) != 0U)
      {
        /* SWS-WIF04/SWS-MOD203: connection dropped during device
           operation (as opposed to still-connecting boot-time churn,
           already handled by s_connect_timeout_handle) — retry for up to
           2 minutes before escalating to a fatal reboot. */
        xEventGroupClearBits(g_sys_events, EVT_WIFI_CONNECTED);
        ESP_LOGW(TAG, "WiFi disconnected after prior connect; reconnecting");
        ceh_NonFatal(CEH_ERR_WIFI_RUNTIME, "wifi disconnected, reconnecting");
        if (timer_OneShot(WIFI_RUNTIME_DISCONNECT_TIMEOUT_MS, runtime_disconnect_timeout_cb,
                          NULL, &s_runtime_disconnect_handle) != RC_SUCCESS)
        {
          ESP_LOGE(TAG, "failed to arm runtime-disconnect timer");
          ceh_Fatal(CEH_ERR_RESOURCE);
        }
      }
      esp_wifi_connect();
      break;
    }

    default:
      break;
  }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
  (void)arg;
  (void)base;

  if (event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT)
  {
    ip_event_assigned_ip_to_client_t *evt =
      (ip_event_assigned_ip_to_client_t *)event_data;
    xEventGroupSetBits(g_sys_events, EVT_WIFI_AP_CLIENT_CONNECTED);
    ESP_LOGI(TAG, "AP client assigned IP: " IPSTR, IP2STR(&evt->ip));
    return;
  }

  timer_Cancel(s_connect_timeout_handle);
  s_connect_timeout_handle = NULL;

  timer_Cancel(s_runtime_disconnect_handle);
  s_runtime_disconnect_handle = NULL;
  ceh_ClearCondition(CEH_ERR_WIFI_RUNTIME);

  xEventGroupSetBits(g_sys_events, EVT_WIFI_CONNECTED);

  ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
  ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
}

static void connect_timeout_cb(void *i_arg)
{
  (void)i_arg;
  s_connect_timeout_handle = NULL;

  ESP_LOGW(TAG, "STA connect timed out; falling back to AP");

  esp_wifi_disconnect();
  start_ap_mode();

  uint32_t fallback_s = NVS_AP_FALLBACK_TIMEOUT_DEFAULT_S;
  nvs_WifiGetApFallbackTimeout(&fallback_s);

  if (timer_OneShot(fallback_s * 1000U, ap_fallback_cb, NULL,
                    &s_ap_fallback_handle) != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "failed to arm AP-fallback timer");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }
}

static void ap_fallback_cb(void *i_arg)
{
  (void)i_arg;
  ESP_LOGE(TAG, "AP fallback timeout expired; rebooting to retry STA");
  ceh_Fatal(CEH_ERR_WIFI_BOOT);
}

static void runtime_disconnect_timeout_cb(void *i_arg)
{
  (void)i_arg;
  ESP_LOGE(TAG, "WiFi runtime reconnect timed out; rebooting");
  ceh_Fatal(CEH_ERR_WIFI_RUNTIME);
}
