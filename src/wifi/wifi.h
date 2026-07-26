/**
 * @file       wifi.h
 * @brief      WiFi connectivity management
 *
 *             Manages the WiFi state machine. Whether to boot into AP or STA
 *             is decided by the caller (mode-control logic in app.c) via
 *             wifi_SetBootMode() — this module does not read NVS to decide
 *             Normal vs Setup mode itself:
 *               - WIFI_BOOT_AP  → AP mode (indefinite).
 *               - WIFI_BOOT_STA → STA connect (credentials read from NVS)
 *                 with a 30 s timeout. On timeout: switch to AP mode + start
 *                 ap_fallback_timeout timer (default 5 min) then reboot to
 *                 retry STA.
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
/*** Types                                                                    */
/******************************************************************************/

/**
 * @brief Which mode wifi_Task should boot into. Set by the caller via
 *        wifi_SetBootMode() — the decision of Normal vs Setup mode is made
 *        by mode-control logic outside this module (see SWS-MOD in the
 *        specification), not by wifi.c itself.
 */
typedef enum
{
  WIFI_BOOT_AP,   /**< Force AP mode; no STA attempt, no fallback timer.     */
  WIFI_BOOT_STA,  /**< Attempt STA connect using credentials read from NVS. */
} wifi_boot_mode_t;

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Initialize the WiFi stack and register event handlers.
 *
 * Must be called before wifi_SetBootMode() and before wifi_Task is created.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t wifi_Init(void);

/**
 * @brief Select which mode wifi_Task will boot into.
 *
 * Must be called after wifi_Init() and before the wifi_Task is created.
 * If never called, wifi_Task defaults to WIFI_BOOT_AP (fail-safe).
 *
 * @param[in] i_mode  Boot mode, decided by the caller.
 */
void wifi_SetBootMode(wifi_boot_mode_t i_mode);

/**
 * @brief WiFi management task.
 *
 * Runs the connect/AP state machine per the mode set with wifi_SetBootMode().
 * Intended to run pinned to Core 1 at priority 6.
 *
 * @param pvParam  Unused; pass NULL when creating the task.
 */
void wifi_Task(void *pvParam);

#endif /* WIFI_H_ */
