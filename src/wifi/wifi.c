/**
 * @file       wifi.c
 * @brief      WiFi connectivity management
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
#include "wifi/wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_handles.h"
#include "wdt/wdt.h"

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t wifi_Init(void)
{
  return RC_SUCCESS;
}

void wifi_Task(void *pvParam)
{
  (void)pvParam;
  wdt_RegisterTask();

  for (;;)
  {
    wdt_Reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
