/**
 * @file       wdt.h
 * @brief      Cooperative software watchdog, plus hardware TWDT fallback
 *
 *             Two layers (SWS-WDT001):
 *             - Primary: each long-running task registers itself with a
 *               per-task timeout (wdt_RegisterTask()) and resets its own
 *               entry from within its normal loop (wdt_Reset()). A
 *               dedicated monitor task periodically checks every
 *               registered task's time since its last reset; if any task
 *               exceeds its allowed interval, ceh_Fatal(CEH_ERR_WATCHDOG)
 *               is called.
 *             - Fallback: ESP-IDF's hardware task watchdog (TWDT), which
 *               this module's own monitor task subscribes to, so that a
 *               frozen monitor task is still caught even though the
 *               primary layer can no longer catch itself.
 *
 *             This module also owns the scheduled-reboot feature
 *             (SWS-WDT002): a plain esp_restart() once the NVS-configured
 *             "Scheduled Reboot Time in minutes" has elapsed since boot (0
 *             = disabled). Not routed through the Central Error Handler,
 *             since it isn't an error condition.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef WDT_H_
#define WDT_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include <stdint.h>

#include "basictypes.h"

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Initialize the watchdog module.
 *
 * Creates the task registry and the monitor task. Called once from
 * app_main before any other tasks are created. Does not (re-)initialise
 * ESP-IDF's hardware TWDT — that is already auto-initialised by this
 * project's sdkconfig; the monitor task just subscribes itself to it.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t wdt_Init(void);

/**
 * @brief Register the calling task with the software watchdog.
 *
 * @param[in] i_timeout_ms  Maximum allowed time between wdt_Reset() calls
 *                          from this task before it is considered stalled.
 *
 * @return RC_SUCCESS or RC_ERROR (registry full).
 */
RC_t wdt_RegisterTask(uint32_t i_timeout_ms);

/**
 * @brief Reset the watchdog for the calling task.
 *
 * Must be called periodically, more often than the timeout passed to
 * wdt_RegisterTask(), from within the task's own loop.
 *
 * @return RC_SUCCESS or RC_ERROR (task not registered).
 */
RC_t wdt_Reset(void);

/**
 * @brief Unregister the calling task from the software watchdog.
 *
 * Called before a task that can legitimately exit deletes itself.
 *
 * @return RC_SUCCESS or RC_ERROR (task not registered).
 */
RC_t wdt_UnregisterTask(void);

#endif /* WDT_H_ */
