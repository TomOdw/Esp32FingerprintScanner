/**
 * @file       nvs.c
 * @brief      Non-volatile storage typed accessors
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
#include "nvs/nvs.h"

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t nvs_Init(void)
{
  return RC_SUCCESS;
}

RC_t nvs_WifiGetSsid(char *o_buf, size_t i_len)
{
  (void)o_buf;
  (void)i_len;
  return RC_BUFFER_EMPTY;
}

RC_t nvs_WifiSetSsid(const char *i_ssid)
{
  (void)i_ssid;
  return RC_SUCCESS;
}

RC_t nvs_WifiGetPassword(char *o_buf, size_t i_len)
{
  (void)o_buf;
  (void)i_len;
  return RC_BUFFER_EMPTY;
}

RC_t nvs_WifiSetPassword(const char *i_password)
{
  (void)i_password;
  return RC_SUCCESS;
}

RC_t nvs_WifiGetApFallbackTimeout(uint32_t *o_seconds)
{
  if (o_seconds != NULL)
  {
    *o_seconds = NVS_AP_FALLBACK_TIMEOUT_DEFAULT_S;
  }
  return RC_SUCCESS;
}

RC_t nvs_WifiSetApFallbackTimeout(uint32_t i_seconds)
{
  (void)i_seconds;
  return RC_SUCCESS;
}

RC_t nvs_MqttGetBroker(char *o_buf, size_t i_len)
{
  (void)o_buf;
  (void)i_len;
  return RC_BUFFER_EMPTY;
}

RC_t nvs_MqttSetBroker(const char *i_broker)
{
  (void)i_broker;
  return RC_SUCCESS;
}

RC_t nvs_MqttGetTopic(char *o_buf, size_t i_len)
{
  (void)o_buf;
  (void)i_len;
  return RC_BUFFER_EMPTY;
}

RC_t nvs_MqttSetTopic(const char *i_topic)
{
  (void)i_topic;
  return RC_SUCCESS;
}

RC_t nvs_MqttGetUser(char *o_buf, size_t i_len)
{
  (void)o_buf;
  (void)i_len;
  return RC_BUFFER_EMPTY;
}

RC_t nvs_MqttSetUser(const char *i_user)
{
  (void)i_user;
  return RC_SUCCESS;
}

RC_t nvs_MqttGetPass(char *o_buf, size_t i_len)
{
  (void)o_buf;
  (void)i_len;
  return RC_BUFFER_EMPTY;
}

RC_t nvs_MqttSetPass(const char *i_pass)
{
  (void)i_pass;
  return RC_SUCCESS;
}

RC_t nvs_MqttGetClientId(char *o_buf, size_t i_len)
{
  (void)o_buf;
  (void)i_len;
  return RC_BUFFER_EMPTY;
}

RC_t nvs_MqttSetClientId(const char *i_client_id)
{
  (void)i_client_id;
  return RC_SUCCESS;
}

RC_t nvs_MqttGetHeartbeatEnabled(bool *o_enabled)
{
  if (o_enabled != NULL)
  {
    *o_enabled = false;
  }
  return RC_SUCCESS;
}

RC_t nvs_MqttSetHeartbeatEnabled(bool i_enabled)
{
  (void)i_enabled;
  return RC_SUCCESS;
}

RC_t nvs_MqttGetHeartbeatTopic(char *o_buf, size_t i_len)
{
  (void)o_buf;
  (void)i_len;
  return RC_BUFFER_EMPTY;
}

RC_t nvs_MqttSetHeartbeatTopic(const char *i_topic)
{
  (void)i_topic;
  return RC_SUCCESS;
}

RC_t nvs_MqttGetHeartbeatInterval(uint32_t *o_seconds)
{
  if (o_seconds != NULL)
  {
    *o_seconds = NVS_HEARTBEAT_INTERVAL_DEFAULT_S;
  }
  return RC_SUCCESS;
}

RC_t nvs_MqttSetHeartbeatInterval(uint32_t i_seconds)
{
  (void)i_seconds;
  return RC_SUCCESS;
}

RC_t nvs_UserGetName(uint8_t i_uuid, char *o_buf, size_t i_len)
{
  (void)i_uuid;
  (void)o_buf;
  (void)i_len;
  return RC_BUFFER_EMPTY;
}

RC_t nvs_UserSetName(uint8_t i_uuid, const char *i_name)
{
  (void)i_uuid;
  (void)i_name;
  return RC_SUCCESS;
}

RC_t nvs_UserDelete(uint8_t i_uuid)
{
  (void)i_uuid;
  return RC_SUCCESS;
}
