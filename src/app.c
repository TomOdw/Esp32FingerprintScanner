/**
 * @file       app.c
 * @brief      Main Application
 *
 *             Boot sequence, global FreeRTOS handle definitions, and
 *             scanner_task implementation.
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
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "basictypes.h"
#include "app_handles.h"
#include "ceh/ceh.h"
#include "wdt/wdt.h"
#include "nvs/nvs.h"
#include "uart/uart.h"
#include "io/io.h"
#include "fps/fps.h"
#include <fpm.h>
#include "wifi/wifi.h"
#include "mqtt/mqtt.h"
#include "cli/cli.h"
#include "telnet/telnet.h"
#include "timer/timer.h"

/******************************************************************************/
/*** Macros and Defines                                                       */
/******************************************************************************/

#define APP_SCANNER_TASK_STACK   4096U
#define APP_WIFI_TASK_STACK      3072U
#define APP_MQTT_TASK_STACK      3072U
#define APP_CLI_TASK_STACK       4096U
#define APP_TELNET_TASK_STACK    3072U

#define APP_SCANNER_TASK_PRIO    5U
#define APP_WIFI_TASK_PRIO       6U
#define APP_MQTT_TASK_PRIO       4U
#define APP_CLI_TASK_PRIO        3U
#define APP_TELNET_TASK_PRIO     3U

#define APP_SCAN_QUEUE_DEPTH     4U
#define APP_CLI_QUEUE_DEPTH      256U

#define APP_SENSOR_BOOT_DELAY_MS 500U

static const char *TAG = "app";

/******************************************************************************/
/*** Global handle definitions (declared extern in app_handles.h)            */
/******************************************************************************/

SemaphoreHandle_t  g_fpm_mutex        = NULL;
SemaphoreHandle_t  g_fp_sense_sem     = NULL;
QueueHandle_t      g_scan_queue       = NULL;
QueueHandle_t      g_cli_input_queue  = NULL;
EventGroupHandle_t g_sys_events       = NULL;

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static void scanner_task(void *pvParam);
static void ceh_led_callback(ceh_err_t i_err);

/******************************************************************************/
/*** Function implementations                                                 */
/******************************************************************************/

void app_main(void)
{
  /* --- Phase 1: Early init (no LED available yet) --- */
  ceh_Init();
  wdt_Init();

  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    nvs_flash_erase();
    nvs_err = nvs_flash_init();
  }
  if (nvs_err != ESP_OK)
  {
    ESP_LOGE(TAG, "nvs_flash_init failed: 0x%x", nvs_err);
    ceh_Fatal(CEH_ERR_NVS_INIT);
  }

  if (nvs_Init() != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "nvs_Init failed");
    ceh_Fatal(CEH_ERR_NVS_INIT);
  }

  /* --- Phase 2: Init IO --- */
  g_fp_sense_sem = xSemaphoreCreateBinary();
  if (g_fp_sense_sem == NULL)
  {
    ESP_LOGE(TAG, "xSemaphoreCreateBinary fp_sense_sem failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  if (io_Init() != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "io_Init failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  ESP_LOGI(TAG, "waiting %u ms for sensor bootup", APP_SENSOR_BOOT_DELAY_MS);
  vTaskDelay(pdMS_TO_TICKS(APP_SENSOR_BOOT_DELAY_MS));

  /* --- Phase 3: FPS sensor init (LED becomes available after this) --- */
  if (uart_Init() != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "uart_Init failed");
    ceh_Fatal(CEH_ERR_FPM_INIT);
  }

  if (fps_Init() != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "fps_Init failed");
    ceh_Fatal(CEH_ERR_FPM_INIT);
  }

  ceh_RegisterLed(ceh_led_callback);

  /* --- Phase 4: Create FreeRTOS resources --- */
  g_fpm_mutex = xSemaphoreCreateBinary();
  if (g_fpm_mutex == NULL)
  {
    ESP_LOGE(TAG, "xSemaphoreCreateBinary fpm_mutex failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }
  xSemaphoreGive(g_fpm_mutex);

  g_scan_queue = xQueueCreate(APP_SCAN_QUEUE_DEPTH, sizeof(mqtt_scan_event_t));
  if (g_scan_queue == NULL)
  {
    ESP_LOGE(TAG, "xQueueCreate scan_queue failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  g_cli_input_queue = xQueueCreate(APP_CLI_QUEUE_DEPTH, sizeof(uint8_t));
  if (g_cli_input_queue == NULL)
  {
    ESP_LOGE(TAG, "xQueueCreate cli_input_queue failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  g_sys_events = xEventGroupCreate();
  if (g_sys_events == NULL)
  {
    ESP_LOGE(TAG, "xEventGroupCreate sys_events failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  /* --- Phase 5: Module pre-task init --- */
  if (wifi_Init() != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "wifi_Init failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  if (cli_Init() != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "cli_Init failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  /* --- Phase 6: Create tasks --- */
  BaseType_t rc;

  rc = xTaskCreatePinnedToCore(scanner_task, "scanner",
                               APP_SCANNER_TASK_STACK, NULL,
                               APP_SCANNER_TASK_PRIO, NULL, 0);
  if (rc != pdPASS) { ESP_LOGE(TAG, "scanner task create failed"); ceh_Fatal(CEH_ERR_RESOURCE); }

  rc = xTaskCreatePinnedToCore(wifi_Task, "wifi",
                               APP_WIFI_TASK_STACK, NULL,
                               APP_WIFI_TASK_PRIO, NULL, 1);
  if (rc != pdPASS) { ESP_LOGE(TAG, "wifi task create failed"); ceh_Fatal(CEH_ERR_RESOURCE); }

  rc = xTaskCreatePinnedToCore(mqtt_Task, "mqtt",
                               APP_MQTT_TASK_STACK, NULL,
                               APP_MQTT_TASK_PRIO, NULL, 1);
  if (rc != pdPASS) { ESP_LOGE(TAG, "mqtt task create failed"); ceh_Fatal(CEH_ERR_RESOURCE); }

  rc = xTaskCreatePinnedToCore(cli_Task, "cli",
                               APP_CLI_TASK_STACK, NULL,
                               APP_CLI_TASK_PRIO, NULL, 0);
  if (rc != pdPASS) { ESP_LOGE(TAG, "cli task create failed"); ceh_Fatal(CEH_ERR_RESOURCE); }

  rc = xTaskCreatePinnedToCore(telnet_Task, "telnet",
                               APP_TELNET_TASK_STACK, NULL,
                               APP_TELNET_TASK_PRIO, NULL, 0);
  if (rc != pdPASS) { ESP_LOGE(TAG, "telnet task create failed"); ceh_Fatal(CEH_ERR_RESOURCE); }

  ESP_LOGI(TAG, "all tasks started");
  vTaskDelete(NULL);
}

/******************************************************************************/

static void scanner_task(void *pvParam)
{
  (void)pvParam;
  wdt_RegisterTask();

  for (;;)
  {
    fps_SetLed(FPM_LED_AWAITING_FINGER);

    xSemaphoreTake(g_fp_sense_sem, portMAX_DELAY);

    uint16_t fp_id = 0;
    uint16_t score = 0;

    if (fps_Lock() != RC_SUCCESS)
    {
      wdt_Reset();
      continue;
    }

    RC_t rc = fps_Scan(&fp_id, &score);
    fps_Unlock();

    if (rc == RC_SUCCESS)
    {
      fpm_fingerprint_meta_t meta = {0};
      fps_Lock();
      fps_raw_read_meta(fp_id, &meta);
      fps_Unlock();

      mqtt_scan_event_t event = {
        .fp_id        = fp_id,
        .score        = score,
        .uuid         = meta.uuid,
        .matched      = true,
        .timestamp_us = 0,  /* esp_timer_get_time() */
      };
      nvs_UserGetName(meta.uuid, event.name, sizeof(event.name));
      xQueueSend(g_scan_queue, &event, 0);

      fps_SetLed(FPM_LED_SCAN_SUCCESS);
    }
    else if (rc == RC_NO_MATCH)
    {
      mqtt_scan_event_t event = {
        .matched      = false,
        .timestamp_us = 0,  /* esp_timer_get_time() */
      };
      xQueueSend(g_scan_queue, &event, 0);

      fps_SetLed(FPM_LED_SCAN_FAILED);
    }
    else
    {
      ESP_LOGW(TAG, "scan error: %d", rc);
      fps_SetLed(FPM_LED_ERROR_1);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    wdt_Reset();
  }
}

static void ceh_led_callback(ceh_err_t i_err)
{
  switch (i_err)
  {
    case CEH_ERR_FPM_INIT:
    case CEH_ERR_NVS_INIT:
      break;
    case CEH_ERR_RESOURCE:
      fps_SetLed(FPM_LED_ERROR_1);
      break;
    case CEH_ERR_WIFI_BOOT:
      fps_SetLed(FPM_LED_ERROR_2);
      break;
    default:
      fps_SetLed(FPM_LED_ERROR_1);
      break;
  }
}
