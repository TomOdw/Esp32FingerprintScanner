/**
 * @file       ceh.c
 * @brief      Critical Event Handler
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
#include "ceh.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "nvs/nvs_app.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/** Total time (us) ceh_Fatal() keeps repeating the LED blink code before
 *  giving up and rebooting anyway (SWS-CEH004). */
#define CEH_FATAL_BLINK_BUDGET_US   (30LL * 1000LL * 1000LL)
/** Pause (ms) between successive blink-code repetitions. */
#define CEH_FATAL_BLINK_PAUSE_MS    2000U

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

static const char *TAG = "ceh";
static ceh_led_cb_t s_led_cb = NULL;

/** SWS-CEH002 dedup state: one "ongoing" flag per error code, not a single
 *  global tracker. A single shared tracker breaks down as soon as two
 *  distinct conditions are both active/flapping (e.g. a WiFi drop
 *  cascading into an MQTT drop) — each one would look "different from
 *  whatever was logged last" and defeat the dedup entirely, flooding the
 *  FIFO exactly like the very problem this is meant to prevent. Indexed
 *  directly by ceh_err_t value; index 0 is unused (no ceh_err_t is 0). */
#define CEH_MAX_ERR_CODE  ((size_t)CEH_ERR_WATCHDOG)
static bool s_condition_ongoing[CEH_MAX_ERR_CODE + 1U];

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static const char *reset_reason_str(esp_reset_reason_t reason);
static void        record_condition(ceh_err_t i_err, const char *i_detail);

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t ceh_Init(void)
{
  esp_reset_reason_t reason = esp_reset_reason();
  ESP_LOGI(TAG, "reset reason: %s (%d)", reset_reason_str(reason), (int)reason);
  return RC_SUCCESS;
}

RC_t ceh_RegisterLed(ceh_led_cb_t i_cb)
{
  if (i_cb == NULL)
  {
    return RC_INVALID_ARG;
  }
  s_led_cb = i_cb;
  return RC_SUCCESS;
}

void ceh_Fatal(ceh_err_t i_err)
{
  ESP_LOGE(TAG, "fatal error %d — rebooting", (int)i_err);

  record_condition(i_err, NULL);

  if (s_led_cb == NULL)
  {
    /* Very early boot (CEH_ERR_FPM_INIT/CEH_ERR_NVS_INIT): no LED to show
       anything on yet, reboot immediately. */
    esp_restart();
  }

  int64_t start_us = esp_timer_get_time();
  do
  {
    s_led_cb(i_err);
    vTaskDelay(pdMS_TO_TICKS(CEH_FATAL_BLINK_PAUSE_MS));
  } while ((esp_timer_get_time() - start_us) < CEH_FATAL_BLINK_BUDGET_US);

  esp_restart();
}

void ceh_NonFatal(ceh_err_t i_err, const char *i_detail)
{
  ESP_LOGW(TAG, "non-fatal condition %d", (int)i_err);

  record_condition(i_err, i_detail);
}

void ceh_ClearCondition(ceh_err_t i_err)
{
  size_t idx = (size_t)i_err;
  if (idx <= CEH_MAX_ERR_CODE)
  {
    s_condition_ongoing[idx] = false;
  }
}

/******************************************************************************/
/*** Local function implementation                                            */
/******************************************************************************/

static const char *reset_reason_str(esp_reset_reason_t reason)
{
  switch (reason)
  {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external reset";
    case ESP_RST_SW:        return "software (esp_restart)";
    case ESP_RST_PANIC:     return "panic / exception";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "wake from deep sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "unknown";
  }
}

/**
 * @brief Push a deduplicated NVS error FIFO entry for i_err (SWS-CEH002).
 *
 * Skips the push if this specific error code's condition is still ongoing
 * (i.e. no ceh_ClearCondition(i_err) call since it was last pushed);
 * otherwise pushes and marks that code's condition as ongoing. Each error
 * code is tracked independently, so an unrelated/different condition
 * occurring in between never resets this one's dedup state. This gating
 * applies only to the NVS FIFO entry — callers are responsible for their
 * own unconditional log line before calling this.
 */
static void record_condition(ceh_err_t i_err, const char *i_detail)
{
  size_t idx = (size_t)i_err;
  if (idx > CEH_MAX_ERR_CODE)
  {
    return;
  }

  /* SWS-NVS005: every raw occurrence increments its code's counter,
     independent of the FIFO dedup below — a condition that stays ongoing
     for a long time and only ever produces one FIFO entry still counts
     every time it was actually raised. */
  nvs_ErrorCodeIncrement((uint8_t)i_err);

  if (s_condition_ongoing[idx])
  {
    return;
  }

  char msg[NVS_ERROR_MSG_MAX_LEN];
  if (i_detail != NULL)
  {
    snprintf(msg, sizeof(msg), "ceh_err %d: %s", (int)i_err, i_detail);
  }
  else
  {
    snprintf(msg, sizeof(msg), "ceh_err %d", (int)i_err);
  }
  nvs_ErrorPush(msg);

  s_condition_ongoing[idx] = true;
}
