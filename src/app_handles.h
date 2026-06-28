/**
 * @file       app_handles.h
 * @brief      Global FreeRTOS handles and system event bit definitions
 *
 *             Declared here, defined in app.c. All modules that need to
 *             interact with shared inter-task primitives include this header.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef APP_HANDLES_H_
#define APP_HANDLES_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

/******************************************************************************/
/*** System event bits                                                        */
/******************************************************************************/

/** WiFi STA connected and IP address obtained */
#define EVT_WIFI_CONNECTED  ((EventBits_t)(1U << 0))

/** Device running in WiFi AP mode */
#define EVT_WIFI_AP_MODE    ((EventBits_t)(1U << 1))

/** MQTT broker connection established */
#define EVT_MQTT_CONNECTED  ((EventBits_t)(1U << 2))

/** Fatal error signalled; reboot imminent */
#define EVT_CEH_FATAL       ((EventBits_t)(1U << 3))

/******************************************************************************/
/*** Shared FreeRTOS handles                                                  */
/******************************************************************************/

/**
 * @brief Binary semaphore guarding exclusive FPM sensor access.
 *
 * Held during an active scan or enrollment sequence. The scanner task
 * acquires it only after the finger-sense GPIO interrupt fires.
 */
extern SemaphoreHandle_t g_fpm_mutex;

/**
 * @brief Binary semaphore given by the FP-sense GPIO ISR.
 *
 * The scanner task blocks on this semaphore while waiting for a finger.
 * Created and owned by io_init().
 */
extern SemaphoreHandle_t g_fp_sense_sem;

/**
 * @brief Queue of scan results from scanner_task to mqtt_task.
 *
 * Item type: mqtt_scan_event_t. Depth: 4.
 */
extern QueueHandle_t g_scan_queue;

/**
 * @brief Byte queue aggregating input from UART and Telnet into cli_task.
 *
 * Depth: 256 bytes.
 */
extern QueueHandle_t g_cli_input_queue;

/**
 * @brief System state event group (EVT_* bits above).
 */
extern EventGroupHandle_t g_sys_events;

#endif /* APP_HANDLES_H_ */
