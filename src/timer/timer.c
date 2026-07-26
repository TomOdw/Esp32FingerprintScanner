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
#include "esp_timer.h"
#include "esp_log.h"

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

static const char *TAG = "timer";

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t timer_OneShot(uint32_t i_timeout_ms, timer_cb_t i_cb, void *i_arg,
                   timer_handle_t *o_handle)
{
  if (i_cb == NULL) return RC_INVALID_ARG;

  esp_timer_create_args_t args = {
    .callback         = i_cb,
    .arg              = i_arg,
    .dispatch_method  = ESP_TIMER_TASK,
    .name             = "oneshot",
  };

  esp_timer_handle_t handle;
  esp_err_t err = esp_timer_create(&args, &handle);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "esp_timer_create failed: 0x%x", err);
    return RC_ERROR;
  }

  err = esp_timer_start_once(handle, (uint64_t)i_timeout_ms * 1000ULL);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "esp_timer_start_once failed: 0x%x", err);
    esp_timer_delete(handle);
    return RC_ERROR;
  }

  if (o_handle != NULL)
  {
    *o_handle = (timer_handle_t)handle;
  }
  return RC_SUCCESS;
}

RC_t timer_Cancel(timer_handle_t i_handle)
{
  if (i_handle == NULL) return RC_SUCCESS;

  esp_timer_handle_t handle = (esp_timer_handle_t)i_handle;

  /* Returns ESP_ERR_INVALID_STATE if the timer already fired or was never
     started as running; either way it is not running afterwards. */
  esp_timer_stop(handle);

  esp_err_t err = esp_timer_delete(handle);
  return (err == ESP_OK) ? RC_SUCCESS : RC_ERROR;
}
