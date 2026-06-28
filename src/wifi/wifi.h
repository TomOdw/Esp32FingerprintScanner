/**
 * @file       wifi.h
 * @brief      WiFi connectivity management
 *
 *             Manages the WiFi state machine:
 *               - No credentials in NVS → AP mode (indefinite).
 *               - Credentials present → STA connect with 30 s timeout.
 *                 On timeout: switch to AP mode + start ap_fallback_timeout
 *                 timer (default 5 min) then reboot to retry STA.
 *               - Post-connect disconnect: ESP-IDF auto-reconnect.
 *
 *             Sets/clears EVT_WIFI_CONNECTED and EVT_WIFI_AP_MODE on g_sys_events.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef WIFI_H_
#define WIFI_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "basictypes.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/** STA connection timeout before switching to AP mode (milliseconds) */
#define WIFI_STA_CONNECT_TIMEOUT_MS  30000U

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Initialize the WiFi stack and register event handlers.
 *
 * Must be called before wifi_Task is created.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t wifi_Init(void);

/**
 * @brief WiFi management task.
 *
 * Reads credentials from NVS and runs the connect/AP state machine.
 * Intended to run pinned to Core 1 at priority 6.
 *
 * @param pvParam  Unused; pass NULL when creating the task.
 */
void wifi_Task(void *pvParam);

#endif /* WIFI_H_ */
