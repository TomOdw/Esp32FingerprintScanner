/**
 * @file       timer.h
 * @brief      One-shot software timer utility
 *
 *             Wrapper over esp_timer for fire-and-forget one-shot timers.
 *             Used by wifi_Task for connect-timeout and AP fallback timeout.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef TIMER_H_
#define TIMER_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include <stdint.h>
#include "basictypes.h"

/******************************************************************************/
/*** Types                                                                    */
/******************************************************************************/

/** Opaque handle to a running one-shot timer. */
typedef void *timer_handle_t;

/** Callback invoked when the timer fires. @p i_arg is the value passed to timer_OneShot(). */
typedef void (*timer_cb_t)(void *i_arg);

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Start a one-shot timer.
 *
 * @param[in]  i_timeout_ms  Timeout in milliseconds.
 * @param[in]  i_cb          Callback invoked on expiry (called from a low-priority
 *                           IDF timer task, not from an ISR).
 * @param[in]  i_arg         Opaque argument passed to @p i_cb.
 * @param[out] o_handle      If non-NULL, receives the timer handle for cancellation.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t timer_OneShot(uint32_t i_timeout_ms, timer_cb_t i_cb, void *i_arg,
                   timer_handle_t *o_handle);

/**
 * @brief Cancel a running one-shot timer.
 *
 * Safe to call after the timer has already fired; a no-op in that case.
 *
 * @param[in] i_handle  Handle returned by timer_OneShot().
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t timer_Cancel(timer_handle_t i_handle);

#endif /* TIMER_H_ */
