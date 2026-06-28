/**
 * @file       timer.c
 * @brief      One-shot software timer utility
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "timer/timer.h"

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t timer_OneShot(uint32_t i_timeout_ms, timer_cb_t i_cb, void *i_arg,
                   timer_handle_t *o_handle)
{
  (void)i_timeout_ms;
  (void)i_cb;
  (void)i_arg;
  (void)o_handle;
  return RC_SUCCESS;
}

RC_t timer_Cancel(timer_handle_t i_handle)
{
  (void)i_handle;
  return RC_SUCCESS;
}
