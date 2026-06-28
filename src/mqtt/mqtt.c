/**
 * @file       mqtt.c
 * @brief      MQTT client task
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
#include "mqtt/mqtt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_handles.h"
#include "wdt/wdt.h"

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

void mqtt_Task(void *pvParam)
{
  (void)pvParam;
  wdt_RegisterTask();

  for (;;)
  {
    xEventGroupWaitBits(g_sys_events, EVT_WIFI_CONNECTED, pdFALSE, pdTRUE,
                        portMAX_DELAY);

    mqtt_scan_event_t event;
    if (xQueueReceive(g_scan_queue, &event, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
      (void)event;
    }

    wdt_Reset();
  }
}
