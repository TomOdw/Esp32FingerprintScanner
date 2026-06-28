/**
 * @file       nvs.h
 * @brief      Non-volatile storage typed accessors
 *
 *             Typed get/set functions for all NVS namespaces:
 *               - "wifi_cfg"  : WiFi credentials and fallback timeout
 *               - "mqtt_cfg"  : MQTT broker, topic, credentials, heartbeat
 *               - "users"     : UUID → display name mapping (0–127)
 *
 *             All string getters require the caller to pass a buffer sized
 *             to the corresponding NVS_*_MAX_LEN constant (including the
 *             null terminator).
 *
 *             RC_BUFFER_EMPTY is returned when a key has never been set.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef NVS_MODULE_H_
#define NVS_MODULE_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "basictypes.h"

/******************************************************************************/
/*** Defines — buffer sizes (include null terminator)                        */
/******************************************************************************/

#define NVS_SSID_MAX_LEN          33U
#define NVS_PASSWORD_MAX_LEN      65U
#define NVS_BROKER_MAX_LEN       129U
#define NVS_TOPIC_MAX_LEN        129U
#define NVS_MQTT_USER_MAX_LEN     65U
#define NVS_MQTT_PASS_MAX_LEN     65U
#define NVS_CLIENT_ID_MAX_LEN     33U
#define NVS_USER_NAME_MAX_LEN     33U

#define NVS_AP_FALLBACK_TIMEOUT_DEFAULT_S  300U
#define NVS_HEARTBEAT_INTERVAL_DEFAULT_S    60U

/******************************************************************************/
/*** API Functions — lifecycle                                                */
/******************************************************************************/

/**
 * @brief Initialize NVS: open handles for all namespaces.
 *
 * Must be called after nvs_flash_init().
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t nvs_Init(void);

/******************************************************************************/
/*** API Functions — wifi_cfg namespace                                      */
/******************************************************************************/

RC_t nvs_WifiGetSsid(char *o_buf, size_t i_len);
RC_t nvs_WifiSetSsid(const char *i_ssid);

RC_t nvs_WifiGetPassword(char *o_buf, size_t i_len);
RC_t nvs_WifiSetPassword(const char *i_password);

RC_t nvs_WifiGetApFallbackTimeout(uint32_t *o_seconds);
RC_t nvs_WifiSetApFallbackTimeout(uint32_t i_seconds);

/******************************************************************************/
/*** API Functions — mqtt_cfg namespace                                      */
/******************************************************************************/

RC_t nvs_MqttGetBroker(char *o_buf, size_t i_len);
RC_t nvs_MqttSetBroker(const char *i_broker);

RC_t nvs_MqttGetTopic(char *o_buf, size_t i_len);
RC_t nvs_MqttSetTopic(const char *i_topic);

RC_t nvs_MqttGetUser(char *o_buf, size_t i_len);
RC_t nvs_MqttSetUser(const char *i_user);

RC_t nvs_MqttGetPass(char *o_buf, size_t i_len);
RC_t nvs_MqttSetPass(const char *i_pass);

RC_t nvs_MqttGetClientId(char *o_buf, size_t i_len);
RC_t nvs_MqttSetClientId(const char *i_client_id);

RC_t nvs_MqttGetHeartbeatEnabled(bool *o_enabled);
RC_t nvs_MqttSetHeartbeatEnabled(bool i_enabled);

RC_t nvs_MqttGetHeartbeatTopic(char *o_buf, size_t i_len);
RC_t nvs_MqttSetHeartbeatTopic(const char *i_topic);

RC_t nvs_MqttGetHeartbeatInterval(uint32_t *o_seconds);
RC_t nvs_MqttSetHeartbeatInterval(uint32_t i_seconds);

/******************************************************************************/
/*** API Functions — users namespace                                         */
/******************************************************************************/

/**
 * @brief Get the display name for a UUID.
 *
 * @param[in]  i_uuid  User UUID (0–127, matching fpm_fingerprint_meta_t.uuid).
 * @param[out] o_buf   Output buffer; must be at least NVS_USER_NAME_MAX_LEN bytes.
 * @param[in]  i_len   Capacity of @p o_buf.
 *
 * @return RC_SUCCESS, RC_BUFFER_EMPTY (not set), or RC_INVALID_ARG.
 */
RC_t nvs_UserGetName(uint8_t i_uuid, char *o_buf, size_t i_len);
RC_t nvs_UserSetName(uint8_t i_uuid, const char *i_name);
RC_t nvs_UserDelete(uint8_t i_uuid);

#endif /* NVS_MODULE_H_ */
