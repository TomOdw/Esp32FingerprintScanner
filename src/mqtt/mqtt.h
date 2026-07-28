/**
 * @file       mqtt.h
 * @brief      MQTT client
 *
 *             mqtt_Init() assembles its connection config from NVS
 *             (broker, credentials, last-will, heartbeat — SWS-NVS002) and
 *             blocks until connected or a timeout elapses (SWS-MQT01).
 *             Once connected, mqtt_Task() drains g_scan_queue and, for
 *             each matched scan event, publishes to the (topic, message)
 *             pair selected by the event's function code (SWS-NVS002's 31
 *             FC slots), substituting {uuid}/{finger_id}/{function_code}/
 *             {score}/{name} placeholders with the event's actual values
 *             (SWS-MQT04). There is no fixed payload shape — the admin
 *             writes the exact topic and message via the webpage's MQTT
 *             Setup screen. A non-matched scan event is not published at
 *             all. A heartbeat is published automatically at the
 *             configured interval, if enabled (SWS-MQT03).
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef MQTT_H_
#define MQTT_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include "basictypes.h"

/******************************************************************************/
/*** Types                                                                    */
/******************************************************************************/

/**
 * @brief Scan result posted by fps_ScanTask() and consumed by mqtt_Task().
 *
 * Placed on g_scan_queue. The uuid-to-name lookup and the fingerprint
 * metadata (uuid/finger_id/function_code) are resolved by the scanner
 * before posting so mqtt_Task never touches NVS or the sensor's fingerprint
 * metadata directly.
 */
typedef struct
{
  uint16_t fp_id;          /**< Matched fingerprint slot ID              */
  uint16_t score;          /**< Match confidence score                   */
  uint8_t  uuid;           /**< UUID from fingerprint metadata           */
  uint16_t finger_id;      /**< Which finger was matched (0-15)          */
  uint8_t  function_code;  /**< Selects the NVS (topic, message) pair (1..31) */
  char     name[32];       /**< Display name resolved from NVS           */
  bool     matched;        /**< true = match found, false = no match     */
  int64_t  timestamp_us;   /**< esp_timer_get_time() at scan completion  */
} mqtt_scan_event_t;

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Initialise the MQTT client and block until connected (SWS-MQT01).
 *
 * Reads the broker, credentials, last-will and heartbeat configuration from
 * NVS itself. Must be called after WiFi is connected.
 *
 * @return RC_SUCCESS once connected, RC_ERROR/RC_TIMEOUT if the broker
 *         could not be reached within the connect timeout.
 */
RC_t mqtt_Init(void);

/**
 * @brief MQTT client task.
 *
 * Drains g_scan_queue and publishes matched scan events (SWS-MQT04).
 * Intended to run pinned to Core 1 at priority 4.
 *
 * @param pvParam  Unused; pass NULL when creating the task.
 */
void mqtt_Task(void *pvParam);

#endif /* MQTT_H_ */
