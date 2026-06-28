/**
 * @file       wdt.h
 * @brief      Task Watchdog Timer wrapper
 *
 *             Thin wrapper over ESP-IDF's task WDT (TWDT). Each FreeRTOS task
 *             registers itself, resets the WDT periodically, and unregisters
 *             on exit.
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
#include "basictypes.h"

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Initialize the task WDT.
 *
 * Reconfigures the TWDT with the application timeout. Called once from
 * app_main before any tasks are created.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t wdt_Init(void);

/**
 * @brief Register the calling task with the WDT.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t wdt_RegisterTask(void);

/**
 * @brief Reset the WDT for the calling task.
 *
 * Must be called periodically within the WDT timeout period.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t wdt_Reset(void);

/**
 * @brief Unregister the calling task from the WDT.
 *
 * Called before a task deletes itself.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t wdt_UnregisterTask(void);

#endif /* WDT_H_ */
