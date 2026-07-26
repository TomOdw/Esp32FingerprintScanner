/**
 * @file       nvs_app.c
 * @brief      Non-volatile storage typed accessors
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.2  Full implementation
 */
/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "nvs/nvs_app.h"
#include "nvs.h"          /* ESP-IDF: nvs_handle_t, nvs_open, nvs_get_str, … */
#include "nvs_flash.h"    /* ESP-IDF: nvs_flash_init, nvs_flash_erase        */
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

/******************************************************************************/
/*** Private types and constants                                              */
/******************************************************************************/

static const char *TAG = "nvs_app";

/* Namespace names */
static const char *NS_WIFI    = "wifi_cfg";
static const char *NS_MQTT    = "mqtt_cfg";
static const char *NS_USERS   = "users";
static const char *NS_GENERAL = "general";
static const char *NS_ERRORS  = "errors";

/* wifi_cfg keys */
static const char *KEY_SSID        = "ssid";
static const char *KEY_WIFI_PASS   = "pass";
static const char *KEY_AP_TIMEOUT  = "ap_timeout";

/* mqtt_cfg keys — fixed entries */
static const char *KEY_BROKER      = "broker";
static const char *KEY_TOPIC       = "topic";
static const char *KEY_MQTT_USER   = "user";
static const char *KEY_MQTT_PASS   = "pass";
static const char *KEY_CLIENT_ID   = "client_id";
static const char *KEY_HB_ENABLED  = "hb_enabled";
static const char *KEY_HB_TOPIC    = "hb_topic";
static const char *KEY_HB_MSG      = "hb_msg";
static const char *KEY_HB_INTERVAL = "hb_interval";
static const char *KEY_LW_ENABLED  = "lw_enabled";
static const char *KEY_LW_TOPIC    = "lw_topic";
static const char *KEY_LW_MSG      = "lw_msg";

/* general keys */
static const char *KEY_REBOOT_MIN     = "reboot_min";
static const char *KEY_SETUP_FLAG     = "setup_flag";
static const char *KEY_ADMIN_ENROLLED = "admin_ok";

/* errors keys */
static const char *KEY_ERR_HEAD    = "err_head";
static const char *KEY_ERR_CNT     = "err_cnt";

/******************************************************************************/
/*** Module-private state                                                     */
/******************************************************************************/

static nvs_handle_t s_wifi_h;
static nvs_handle_t s_mqtt_h;
static nvs_handle_t s_users_h;
static nvs_handle_t s_general_h;
static nvs_handle_t s_errors_h;

/******************************************************************************/
/*** Private helper functions                                                 */
/******************************************************************************/

static RC_t get_str(nvs_handle_t h, const char *key, char *o_buf, size_t i_len)
{
  size_t len = i_len;
  esp_err_t err = nvs_get_str(h, key, o_buf, &len);
  if (err == ESP_ERR_NVS_NOT_FOUND) return RC_BUFFER_EMPTY;
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "get_str '%s' failed: 0x%x", key, err);
    return RC_ERROR;
  }
  return RC_SUCCESS;
}

static RC_t set_str(nvs_handle_t h, const char *key, const char *val)
{
  esp_err_t err = nvs_set_str(h, key, val);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "set_str '%s' failed: 0x%x", key, err);
    return RC_ERROR;
  }
  err = nvs_commit(h);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "commit after set_str '%s' failed: 0x%x", key, err);
    return RC_ERROR;
  }
  return RC_SUCCESS;
}

static RC_t get_u32(nvs_handle_t h, const char *key, uint32_t def, uint32_t *out)
{
  esp_err_t err = nvs_get_u32(h, key, out);
  if (err == ESP_ERR_NVS_NOT_FOUND) { *out = def; return RC_SUCCESS; }
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "get_u32 '%s' failed: 0x%x", key, err);
    return RC_ERROR;
  }
  return RC_SUCCESS;
}

static RC_t set_u32(nvs_handle_t h, const char *key, uint32_t val)
{
  esp_err_t err = nvs_set_u32(h, key, val);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "set_u32 '%s' failed: 0x%x", key, err);
    return RC_ERROR;
  }
  err = nvs_commit(h);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "commit after set_u32 '%s' failed: 0x%x", key, err);
    return RC_ERROR;
  }
  return RC_SUCCESS;
}

static RC_t get_bool(nvs_handle_t h, const char *key, bool def, bool *out)
{
  uint8_t v = 0;
  esp_err_t err = nvs_get_u8(h, key, &v);
  if (err == ESP_ERR_NVS_NOT_FOUND) { *out = def; return RC_SUCCESS; }
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "get_bool '%s' failed: 0x%x", key, err);
    return RC_ERROR;
  }
  *out = (v != 0U);
  return RC_SUCCESS;
}

static RC_t set_bool(nvs_handle_t h, const char *key, bool val)
{
  esp_err_t err = nvs_set_u8(h, key, val ? 1U : 0U);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "set_bool '%s' failed: 0x%x", key, err);
    return RC_ERROR;
  }
  err = nvs_commit(h);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "commit after set_bool '%s' failed: 0x%x", key, err);
    return RC_ERROR;
  }
  return RC_SUCCESS;
}

/******************************************************************************/
/*** Lifecycle                                                                */
/******************************************************************************/

RC_t nvs_Init(void)
{
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_LOGW(TAG, "nvs_flash_init needs erase (0x%x)", err);
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "nvs_flash_init failed: 0x%x", err);
    return RC_ERROR;
  }

  if (nvs_open(NS_WIFI,    NVS_READWRITE, &s_wifi_h)    != ESP_OK) return RC_ERROR;
  if (nvs_open(NS_MQTT,    NVS_READWRITE, &s_mqtt_h)    != ESP_OK) return RC_ERROR;
  if (nvs_open(NS_USERS,   NVS_READWRITE, &s_users_h)   != ESP_OK) return RC_ERROR;
  if (nvs_open(NS_GENERAL, NVS_READWRITE, &s_general_h) != ESP_OK) return RC_ERROR;
  if (nvs_open(NS_ERRORS,  NVS_READWRITE, &s_errors_h)  != ESP_OK) return RC_ERROR;

  ESP_LOGI(TAG, "initialised");
  return RC_SUCCESS;
}

/******************************************************************************/
/*** wifi_cfg namespace                                                      */
/******************************************************************************/

RC_t nvs_WifiGetSsid(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_wifi_h, KEY_SSID, o_buf, i_len);
}

RC_t nvs_WifiSetSsid(const char *i_ssid)
{
  if (i_ssid == NULL) return RC_INVALID_ARG;
  return set_str(s_wifi_h, KEY_SSID, i_ssid);
}

RC_t nvs_WifiGetPassword(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_wifi_h, KEY_WIFI_PASS, o_buf, i_len);
}

RC_t nvs_WifiSetPassword(const char *i_password)
{
  if (i_password == NULL) return RC_INVALID_ARG;
  return set_str(s_wifi_h, KEY_WIFI_PASS, i_password);
}

RC_t nvs_WifiGetApFallbackTimeout(uint32_t *o_seconds)
{
  if (o_seconds == NULL) return RC_INVALID_ARG;
  return get_u32(s_wifi_h, KEY_AP_TIMEOUT, NVS_AP_FALLBACK_TIMEOUT_DEFAULT_S, o_seconds);
}

RC_t nvs_WifiSetApFallbackTimeout(uint32_t i_seconds)
{
  return set_u32(s_wifi_h, KEY_AP_TIMEOUT, i_seconds);
}

/******************************************************************************/
/*** mqtt_cfg namespace — credentials and topic                              */
/******************************************************************************/

RC_t nvs_MqttGetBroker(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_mqtt_h, KEY_BROKER, o_buf, i_len);
}

RC_t nvs_MqttSetBroker(const char *i_broker)
{
  if (i_broker == NULL) return RC_INVALID_ARG;
  return set_str(s_mqtt_h, KEY_BROKER, i_broker);
}

RC_t nvs_MqttGetTopic(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_mqtt_h, KEY_TOPIC, o_buf, i_len);
}

RC_t nvs_MqttSetTopic(const char *i_topic)
{
  if (i_topic == NULL) return RC_INVALID_ARG;
  return set_str(s_mqtt_h, KEY_TOPIC, i_topic);
}

RC_t nvs_MqttGetUser(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_mqtt_h, KEY_MQTT_USER, o_buf, i_len);
}

RC_t nvs_MqttSetUser(const char *i_user)
{
  if (i_user == NULL) return RC_INVALID_ARG;
  return set_str(s_mqtt_h, KEY_MQTT_USER, i_user);
}

RC_t nvs_MqttGetPass(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_mqtt_h, KEY_MQTT_PASS, o_buf, i_len);
}

RC_t nvs_MqttSetPass(const char *i_pass)
{
  if (i_pass == NULL) return RC_INVALID_ARG;
  return set_str(s_mqtt_h, KEY_MQTT_PASS, i_pass);
}

RC_t nvs_MqttGetClientId(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_mqtt_h, KEY_CLIENT_ID, o_buf, i_len);
}

RC_t nvs_MqttSetClientId(const char *i_client_id)
{
  if (i_client_id == NULL) return RC_INVALID_ARG;
  return set_str(s_mqtt_h, KEY_CLIENT_ID, i_client_id);
}

/******************************************************************************/
/*** mqtt_cfg namespace — heartbeat                                          */
/******************************************************************************/

RC_t nvs_MqttGetHeartbeatEnabled(bool *o_enabled)
{
  if (o_enabled == NULL) return RC_INVALID_ARG;
  return get_bool(s_mqtt_h, KEY_HB_ENABLED, false, o_enabled);
}

RC_t nvs_MqttSetHeartbeatEnabled(bool i_enabled)
{
  return set_bool(s_mqtt_h, KEY_HB_ENABLED, i_enabled);
}

RC_t nvs_MqttGetHeartbeatTopic(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_mqtt_h, KEY_HB_TOPIC, o_buf, i_len);
}

RC_t nvs_MqttSetHeartbeatTopic(const char *i_topic)
{
  if (i_topic == NULL) return RC_INVALID_ARG;
  return set_str(s_mqtt_h, KEY_HB_TOPIC, i_topic);
}

RC_t nvs_MqttGetHeartbeatMessage(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_mqtt_h, KEY_HB_MSG, o_buf, i_len);
}

RC_t nvs_MqttSetHeartbeatMessage(const char *i_msg)
{
  if (i_msg == NULL) return RC_INVALID_ARG;
  return set_str(s_mqtt_h, KEY_HB_MSG, i_msg);
}

RC_t nvs_MqttGetHeartbeatInterval(uint32_t *o_seconds)
{
  if (o_seconds == NULL) return RC_INVALID_ARG;
  return get_u32(s_mqtt_h, KEY_HB_INTERVAL, NVS_HEARTBEAT_INTERVAL_DEFAULT_S, o_seconds);
}

RC_t nvs_MqttSetHeartbeatInterval(uint32_t i_seconds)
{
  return set_u32(s_mqtt_h, KEY_HB_INTERVAL, i_seconds);
}

/******************************************************************************/
/*** mqtt_cfg namespace — last-will                                          */
/******************************************************************************/

RC_t nvs_MqttGetLastWillEnabled(bool *o_enabled)
{
  if (o_enabled == NULL) return RC_INVALID_ARG;
  return get_bool(s_mqtt_h, KEY_LW_ENABLED, false, o_enabled);
}

RC_t nvs_MqttSetLastWillEnabled(bool i_enabled)
{
  return set_bool(s_mqtt_h, KEY_LW_ENABLED, i_enabled);
}

RC_t nvs_MqttGetLastWillTopic(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_mqtt_h, KEY_LW_TOPIC, o_buf, i_len);
}

RC_t nvs_MqttSetLastWillTopic(const char *i_topic)
{
  if (i_topic == NULL) return RC_INVALID_ARG;
  return set_str(s_mqtt_h, KEY_LW_TOPIC, i_topic);
}

RC_t nvs_MqttGetLastWillMessage(char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;
  return get_str(s_mqtt_h, KEY_LW_MSG, o_buf, i_len);
}

RC_t nvs_MqttSetLastWillMessage(const char *i_msg)
{
  if (i_msg == NULL) return RC_INVALID_ARG;
  return set_str(s_mqtt_h, KEY_LW_MSG, i_msg);
}

/******************************************************************************/
/*** mqtt_cfg namespace — function codes                                     */
/******************************************************************************/

RC_t nvs_MqttGetFcTopic(uint8_t i_fc, char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U || i_fc < NVS_FC_MIN || i_fc > NVS_FC_MAX)
    return RC_INVALID_ARG;
  char key[8];
  snprintf(key, sizeof(key), "fc%02d_t", (int)i_fc);
  return get_str(s_mqtt_h, key, o_buf, i_len);
}

RC_t nvs_MqttSetFcTopic(uint8_t i_fc, const char *i_topic)
{
  if (i_topic == NULL || i_fc < NVS_FC_MIN || i_fc > NVS_FC_MAX) return RC_INVALID_ARG;
  char key[8];
  snprintf(key, sizeof(key), "fc%02d_t", (int)i_fc);
  return set_str(s_mqtt_h, key, i_topic);
}

RC_t nvs_MqttGetFcMessage(uint8_t i_fc, char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U || i_fc < NVS_FC_MIN || i_fc > NVS_FC_MAX)
    return RC_INVALID_ARG;
  char key[8];
  snprintf(key, sizeof(key), "fc%02d_m", (int)i_fc);
  return get_str(s_mqtt_h, key, o_buf, i_len);
}

RC_t nvs_MqttSetFcMessage(uint8_t i_fc, const char *i_msg)
{
  if (i_msg == NULL || i_fc < NVS_FC_MIN || i_fc > NVS_FC_MAX) return RC_INVALID_ARG;
  char key[8];
  snprintf(key, sizeof(key), "fc%02d_m", (int)i_fc);
  return set_str(s_mqtt_h, key, i_msg);
}

/******************************************************************************/
/*** users namespace                                                         */
/******************************************************************************/

RC_t nvs_UserGetName(uint8_t i_uuid, char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U || i_uuid == 0U || i_uuid > 127U) return RC_INVALID_ARG;
  char key[6];
  snprintf(key, sizeof(key), "u%03d", (int)i_uuid);
  return get_str(s_users_h, key, o_buf, i_len);
}

RC_t nvs_UserSetName(uint8_t i_uuid, const char *i_name)
{
  if (i_name == NULL || i_uuid == 0U || i_uuid > 127U) return RC_INVALID_ARG;
  char key[6];
  snprintf(key, sizeof(key), "u%03d", (int)i_uuid);
  return set_str(s_users_h, key, i_name);
}

RC_t nvs_UserDelete(uint8_t i_uuid)
{
  if (i_uuid == 0U || i_uuid > 127U) return RC_INVALID_ARG;
  char key[6];
  snprintf(key, sizeof(key), "u%03d", (int)i_uuid);
  esp_err_t err = nvs_erase_key(s_users_h, key);
  if (err == ESP_ERR_NVS_NOT_FOUND) return RC_SUCCESS;
  if (err != ESP_OK) return RC_ERROR;
  err = nvs_commit(s_users_h);
  return (err == ESP_OK) ? RC_SUCCESS : RC_ERROR;
}

RC_t nvs_UserList(nvs_user_entry_t *o_entries, size_t i_max_entries, size_t *o_count)
{
  if (o_entries == NULL || o_count == NULL) return RC_INVALID_ARG;

  size_t count = 0;
  nvs_iterator_t it = NULL;
  esp_err_t err = nvs_entry_find_in_handle(s_users_h, NVS_TYPE_STR, &it);

  while (err == ESP_OK && it != NULL && count < i_max_entries)
  {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);

    unsigned uuid = 0;
    if (sscanf(info.key, "u%03u", &uuid) == 1 && uuid >= 1U && uuid <= 127U)
    {
      o_entries[count].uuid = (uint8_t)uuid;
      size_t len = sizeof(o_entries[count].name);
      if (nvs_get_str(s_users_h, info.key, o_entries[count].name, &len) == ESP_OK)
      {
        count++;
      }
    }

    err = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);

  *o_count = count;
  return RC_SUCCESS;
}

/******************************************************************************/
/*** general namespace                                                       */
/******************************************************************************/

RC_t nvs_GeneralGetRebootMinutes(uint32_t *o_minutes)
{
  if (o_minutes == NULL) return RC_INVALID_ARG;
  return get_u32(s_general_h, KEY_REBOOT_MIN, NVS_SCHEDULED_REBOOT_DEFAULT_MIN, o_minutes);
}

RC_t nvs_GeneralSetRebootMinutes(uint32_t i_minutes)
{
  return set_u32(s_general_h, KEY_REBOOT_MIN, i_minutes);
}

RC_t nvs_GeneralGetSetupFlag(bool *o_flag)
{
  if (o_flag == NULL) return RC_INVALID_ARG;
  return get_bool(s_general_h, KEY_SETUP_FLAG, false, o_flag);
}

RC_t nvs_GeneralSetSetupFlag(bool i_flag)
{
  return set_bool(s_general_h, KEY_SETUP_FLAG, i_flag);
}

RC_t nvs_GeneralGetAdminEnrolled(bool *o_flag)
{
  if (o_flag == NULL) return RC_INVALID_ARG;
  return get_bool(s_general_h, KEY_ADMIN_ENROLLED, false, o_flag);
}

RC_t nvs_GeneralSetAdminEnrolled(bool i_flag)
{
  return set_bool(s_general_h, KEY_ADMIN_ENROLLED, i_flag);
}

/******************************************************************************/
/*** errors namespace — 10-slot FIFO ring buffer                            */
/******************************************************************************/

RC_t nvs_ErrorPush(const char *i_msg)
{
  if (i_msg == NULL) return RC_INVALID_ARG;

  uint8_t head = 0U;
  uint8_t cnt  = 0U;
  nvs_get_u8(s_errors_h, KEY_ERR_HEAD, &head);
  nvs_get_u8(s_errors_h, KEY_ERR_CNT,  &cnt);

  uint8_t write_slot;
  if (cnt < NVS_ERROR_SLOT_COUNT)
  {
    write_slot = (uint8_t)((head + cnt) % NVS_ERROR_SLOT_COUNT);
    cnt++;
  }
  else
  {
    /* Overwrite oldest; advance head */
    write_slot = head;
    head = (uint8_t)((head + 1U) % NVS_ERROR_SLOT_COUNT);
  }

  char key[8];
  snprintf(key, sizeof(key), "err_%d", (int)write_slot);

  esp_err_t err = nvs_set_str(s_errors_h, key, i_msg);
  if (err != ESP_OK) return RC_ERROR;

  nvs_set_u8(s_errors_h, KEY_ERR_HEAD, head);
  nvs_set_u8(s_errors_h, KEY_ERR_CNT,  cnt);
  err = nvs_commit(s_errors_h);
  return (err == ESP_OK) ? RC_SUCCESS : RC_ERROR;
}

RC_t nvs_ErrorGet(uint8_t i_slot, char *o_buf, size_t i_len)
{
  if (o_buf == NULL || i_len == 0U) return RC_INVALID_ARG;

  uint8_t head = 0U;
  uint8_t cnt  = 0U;
  nvs_get_u8(s_errors_h, KEY_ERR_HEAD, &head);
  nvs_get_u8(s_errors_h, KEY_ERR_CNT,  &cnt);

  if (i_slot >= cnt) return RC_INVALID_ARG;

  uint8_t phys = (uint8_t)((head + i_slot) % NVS_ERROR_SLOT_COUNT);
  char key[8];
  snprintf(key, sizeof(key), "err_%d", (int)phys);

  size_t len = i_len;
  esp_err_t err = nvs_get_str(s_errors_h, key, o_buf, &len);
  return (err == ESP_OK) ? RC_SUCCESS : RC_ERROR;
}

RC_t nvs_ErrorGetCount(uint8_t *o_count)
{
  if (o_count == NULL) return RC_INVALID_ARG;
  *o_count = 0U;
  nvs_get_u8(s_errors_h, KEY_ERR_CNT, o_count);
  return RC_SUCCESS;
}

RC_t nvs_ErrorClear(void)
{
  nvs_set_u8(s_errors_h, KEY_ERR_HEAD, 0U);
  nvs_set_u8(s_errors_h, KEY_ERR_CNT,  0U);
  esp_err_t err = nvs_commit(s_errors_h);
  return (err == ESP_OK) ? RC_SUCCESS : RC_ERROR;
}
