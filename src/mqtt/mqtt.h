/**
 * @file       mqtt.h
 * @brief      MQTT client task
 *
 *             Waits for EVT_WIFI_CONNECTED, then connects to the broker
 *             configured in NVS. Drains g_scan_queue and publishes scan events
 *             as JSON. Optionally publishes a periodic heartbeat.
 *
 *             Publish payload (scan event, topic from NVS):
 *             {
 *               "id":        <fp_id>,
 *               "uuid":      <uuid>,
 *               "name":      "<display name>",
 *               "score":     <score>,
 *               "matched":   <true|false>,
 *               "timestamp": <microseconds since boot>
 *             }
 *
 *             Heartbeat payload (separate configurable topic):
 *             { "uptime_s": <seconds since boot> }
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
 * @brief Scan result posted by scanner_task and consumed by mqtt_Task.
 *
 * Placed on g_scan_queue. The uuid-to-name lookup is performed by
 * scanner_task before posting so mqtt_Task never touches NVS.
 */
typedef struct
{
  uint16_t fp_id;        /**< Matched fingerprint slot ID            */
  uint16_t score;        /**< Match confidence score                 */
  uint8_t  uuid;         /**< UUID from fingerprint metadata         */
  char     name[32];     /**< Display name resolved from NVS         */
  bool     matched;      /**< true = match found, false = no match   */
  int64_t  timestamp_us; /**< esp_timer_get_time() at scan completion */
} mqtt_scan_event_t;

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief MQTT client task.
 *
 * Intended to run pinned to Core 1 at priority 4.
 *
 * @param pvParam  Unused; pass NULL when creating the task.
 */
void mqtt_Task(void *pvParam);

#endif /* MQTT_H_ */
