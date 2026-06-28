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

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

static const char *TAG = "ceh";
static ceh_led_cb_t s_led_cb = NULL;

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static const char *reset_reason_str(esp_reset_reason_t reason);

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

  if (s_led_cb != NULL)
  {
    s_led_cb(i_err);
    vTaskDelay(pdMS_TO_TICKS(3000));
  }

  esp_restart();
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
