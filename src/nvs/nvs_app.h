/**
 * @file       nvs_app.h
 * @brief      Non-volatile storage typed accessors
 *
 *             Typed get/set functions for all NVS namespaces:
 *               - "wifi_cfg"  : WiFi credentials and fallback timeout
 *               - "mqtt_cfg"  : MQTT broker, topic, credentials, heartbeat,
 *                               last-will, and function codes (FC 1..31)
 *               - "users"     : UUID → display name mapping (1..127)
 *               - "general"   : scheduled reboot time and setup-enter flag
 *               - "errors"    : 10-slot FIFO of error messages
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
 * @version    0.2  Full implementation (replaces stub nvs.h)
 */
#ifndef NVS_APP_H_
#define NVS_APP_H_

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

#define NVS_SSID_MAX_LEN              33U
#define NVS_PASSWORD_MAX_LEN          65U
#define NVS_BROKER_MAX_LEN           129U
#define NVS_TOPIC_MAX_LEN            129U
#define NVS_MQTT_USER_MAX_LEN         65U
#define NVS_MQTT_PASS_MAX_LEN         65U
#define NVS_CLIENT_ID_MAX_LEN         33U
#define NVS_USER_NAME_MAX_LEN         33U
#define NVS_MQTT_MSG_MAX_LEN         129U   /**< MQTT payload (heartbeat/LW/FC) */

/******************************************************************************/
/*** Defines — function codes                                                 */
/******************************************************************************/

#define NVS_FC_MIN                     1U
#define NVS_FC_MAX                    31U
#define NVS_FC_COUNT                  31U

/******************************************************************************/
/*** Defines — error FIFO                                                     */
/******************************************************************************/

#define NVS_ERROR_MSG_MAX_LEN        129U
#define NVS_ERROR_SLOT_COUNT          10U

/******************************************************************************/
/*** Defines — default values                                                 */
/******************************************************************************/

#define NVS_AP_FALLBACK_TIMEOUT_DEFAULT_S     300U
#define NVS_HEARTBEAT_INTERVAL_DEFAULT_S       60U
#define NVS_SCHEDULED_REBOOT_DEFAULT_MIN        0U   /**< 0 = disabled */

/******************************************************************************/
/*** API Functions — lifecycle                                                */
/******************************************************************************/

/**
 * @brief Initialise NVS: run nvs_flash_init() and open handles for all
 *        namespaces.  Must be called before any other nvs_* function.
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

/* Heartbeat */
RC_t nvs_MqttGetHeartbeatEnabled(bool *o_enabled);
RC_t nvs_MqttSetHeartbeatEnabled(bool i_enabled);

RC_t nvs_MqttGetHeartbeatTopic(char *o_buf, size_t i_len);
RC_t nvs_MqttSetHeartbeatTopic(const char *i_topic);

RC_t nvs_MqttGetHeartbeatMessage(char *o_buf, size_t i_len);
RC_t nvs_MqttSetHeartbeatMessage(const char *i_msg);

RC_t nvs_MqttGetHeartbeatInterval(uint32_t *o_seconds);
RC_t nvs_MqttSetHeartbeatInterval(uint32_t i_seconds);

/* Last-will */
RC_t nvs_MqttGetLastWillEnabled(bool *o_enabled);
RC_t nvs_MqttSetLastWillEnabled(bool i_enabled);

RC_t nvs_MqttGetLastWillTopic(char *o_buf, size_t i_len);
RC_t nvs_MqttSetLastWillTopic(const char *i_topic);

RC_t nvs_MqttGetLastWillMessage(char *o_buf, size_t i_len);
RC_t nvs_MqttSetLastWillMessage(const char *i_msg);

/* Function codes — i_fc must be in [NVS_FC_MIN, NVS_FC_MAX] */
RC_t nvs_MqttGetFcTopic(uint8_t i_fc, char *o_buf, size_t i_len);
RC_t nvs_MqttSetFcTopic(uint8_t i_fc, const char *i_topic);

RC_t nvs_MqttGetFcMessage(uint8_t i_fc, char *o_buf, size_t i_len);
RC_t nvs_MqttSetFcMessage(uint8_t i_fc, const char *i_msg);

/******************************************************************************/
/*** API Functions — users namespace                                         */
/******************************************************************************/

/**
 * @brief Get the display name for a UUID.
 *
 * @param[in]  i_uuid  User UUID (1–127; 0 marks empty slots and is invalid).
 * @param[out] o_buf   Output buffer; must be at least NVS_USER_NAME_MAX_LEN bytes.
 * @param[in]  i_len   Capacity of @p o_buf.
 *
 * @return RC_SUCCESS, RC_BUFFER_EMPTY (not set), or RC_INVALID_ARG.
 */
RC_t nvs_UserGetName(uint8_t i_uuid, char *o_buf, size_t i_len);
RC_t nvs_UserSetName(uint8_t i_uuid, const char *i_name);
RC_t nvs_UserDelete(uint8_t i_uuid);

/******************************************************************************/
/*** API Functions — general namespace                                       */
/******************************************************************************/

/**
 * @brief Scheduled reboot interval.  0 means disabled.
 */
RC_t nvs_GeneralGetRebootMinutes(uint32_t *o_minutes);
RC_t nvs_GeneralSetRebootMinutes(uint32_t i_minutes);

/**
 * @brief Setup-enter flag.  When true the device boots into setup mode.
 *        The flag is cleared by the mode-control logic after acting on it
 *        (SWS-MOD04).
 */
RC_t nvs_GeneralGetSetupFlag(bool *o_flag);
RC_t nvs_GeneralSetSetupFlag(bool i_flag);

/******************************************************************************/
/*** API Functions — error FIFO                                              */
/******************************************************************************/

/**
 * @brief Push an error message into the FIFO.
 *        When the FIFO is full the oldest entry is overwritten.
 */
RC_t nvs_ErrorPush(const char *i_msg);

/**
 * @brief Read a FIFO entry by logical index (0 = oldest, count-1 = newest).
 */
RC_t nvs_ErrorGet(uint8_t i_slot, char *o_buf, size_t i_len);

/**
 * @brief Return the number of entries currently stored (0–NVS_ERROR_SLOT_COUNT).
 */
RC_t nvs_ErrorGetCount(uint8_t *o_count);

/**
 * @brief Clear all error FIFO entries.
 */
RC_t nvs_ErrorClear(void);

#endif /* NVS_APP_H_ */
