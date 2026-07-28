/**
 * @file       app.c
 * @brief      Main Application
 *
 *             Boot sequence and global FreeRTOS handle definitions. The
 *             Normal-Mode scan task itself lives in fps.c (fps_ScanTask).
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
#include "esp_log.h"

#include "basictypes.h"
#include "app_handles.h"
#include "ceh/ceh.h"
#include "wdt/wdt.h"
#include "nvs/nvs_app.h"
#include "uart/uart.h"
#include "io/io.h"
#include "fps/fps.h"
#include <fpm.h>
#include "wifi/wifi.h"
#include "mqtt/mqtt.h"
#include "timer/timer.h"
#include "mode/mode.h"
#include "webpage/webpage.h"

/******************************************************************************/
/*** Macros and Defines                                                       */
/******************************************************************************/

#define APP_SCANNER_TASK_STACK   4096U
#define APP_WIFI_TASK_STACK      3072U
#define APP_MQTT_TASK_STACK      3072U
#define APP_WEBPAGE_START_STACK  2048U

#define APP_SCANNER_TASK_PRIO    5U
#define APP_WIFI_TASK_PRIO       6U
#define APP_MQTT_TASK_PRIO       4U
#define APP_WEBPAGE_START_PRIO   3U

#define APP_SCAN_QUEUE_DEPTH     4U

#define APP_SENSOR_BOOT_DELAY_MS 500U

/** SWS-MOD202: how long the Normal-Mode boot sequence waits for WiFi
 *  before proceeding to the MQTT stage. A bit above wifi.c's own internal
 *  WIFI_STA_CONNECT_TIMEOUT_MS (30s) — on a genuine boot-time failure,
 *  wifi.c's own state machine has already ceh_Fatal()'d (reboots) well
 *  before this would otherwise elapse. */
#define APP_WIFI_BOOT_WAIT_TIMEOUT_MS 35000U

/** SWS-MOD004: settle time before the boot-time admin-finger identification
 *  scan, mirroring webpage.c's SCAN_CAPTURE_SETTLE_MS — capturing the
 *  instant presence is observed (rather than letting the finger seat
 *  first) is known to produce a poor first image. */
#define APP_BOOT_ADMIN_SCAN_SETTLE_MS  600U

/** How long the "op success" LED confirmation is held once the boot-time
 *  admin-finger override is confirmed, before proceeding into Setup-Mode. */
#define APP_BOOT_ADMIN_LED_CONFIRM_MS 1000U

static const char *TAG = "app";

/******************************************************************************/
/*** Global handle definitions (declared extern in app_handles.h)            */
/******************************************************************************/

SemaphoreHandle_t  g_fpm_mutex        = NULL;
SemaphoreHandle_t  g_fp_sense_sem     = NULL;
QueueHandle_t      g_scan_queue       = NULL;
EventGroupHandle_t g_sys_events       = NULL;

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static void webpage_start_task(void *pvParam);
static void ceh_led_callback(ceh_err_t i_err);

/******************************************************************************/
/*** Function implementations                                                 */
/******************************************************************************/

void app_main(void)
{
  /* --- Phase 1: Early init (no LED available yet) --- */
  ceh_Init();
  wdt_Init();

  if (nvs_Init() != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "nvs_Init failed");
    ceh_Fatal(CEH_ERR_NVS_INIT);
  }

  app_mode_t boot_mode = app_mode_Decide(); /* SWS-MOD002/003 */

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
  /* Moved ahead of the SWS-MOD004 admin-finger check below: fps_Lock()
     needs g_fpm_mutex to already exist. */
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

  g_sys_events = xEventGroupCreate();
  if (g_sys_events == NULL)
  {
    ESP_LOGE(TAG, "xEventGroupCreate sys_events failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  /* SWS-MOD004: a finger already resting on the sensor at boot, before
     WiFi is even touched, overrides a would-be Normal-Mode boot into
     Setup-Mode if it's the admin/master fingerprint — an escape hatch out
     of a WiFi/MQTT connect-failure boot loop (misconfigured, not merely
     unset, broker/credentials) with no other way to reach the webpage. A
     raw presence read (not io_WaitFingerPresent(): there is no
     "not-present" baseline yet this early in boot) so this costs nothing
     when nobody is touching the sensor. */
  if (boot_mode == APP_MODE_NORMAL && io_FpSensePresent())
  {
    vTaskDelay(pdMS_TO_TICKS(APP_BOOT_ADMIN_SCAN_SETTLE_MS));

    uint16_t fp_id = 0U;
    uint16_t score = 0U;
    RC_t     scan_rc = RC_ERROR;
    if (fps_Lock() == RC_SUCCESS)
    {
      scan_rc = fps_Scan(&fp_id, &score);
      fps_Unlock();
    }

    if (scan_rc == RC_SUCCESS)
    {
      fpm_fingerprint_meta_t meta = {0};
      fps_Lock();
      fps_raw_read_meta(fp_id, &meta);
      fps_Unlock();

      if (meta.finger_id == FPS_ADMIN_FINGER_ID)
      {
        ESP_LOGI(TAG, "admin fingerprint present at boot; forcing Setup-Mode");
        fps_SetLed(FPM_LED_OP_SUCCESS);
        vTaskDelay(pdMS_TO_TICKS(APP_BOOT_ADMIN_LED_CONFIRM_MS));
        boot_mode = APP_MODE_SETUP;
      }
    }
  }

  if (boot_mode == APP_MODE_SETUP)
  {
    app_mode_EnterSetup(); /* SWS-MOD104: consume the setup-enter flag now */
  }

  /* --- Phase 5: Module pre-task init --- */
  if (wifi_Init() != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "wifi_Init failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  wifi_SetBootMode((boot_mode == APP_MODE_SETUP) ? WIFI_BOOT_AP : WIFI_BOOT_STA);

  /* --- Phase 6: Create tasks --- */
  BaseType_t rc;

  rc = xTaskCreatePinnedToCore(wifi_Task, "wifi",
                               APP_WIFI_TASK_STACK, NULL,
                               APP_WIFI_TASK_PRIO, NULL, 1);
  if (rc != pdPASS) { ESP_LOGE(TAG, "wifi task create failed"); ceh_Fatal(CEH_ERR_RESOURCE); }

  if (boot_mode == APP_MODE_NORMAL)
  {
    /* SWS-MOD201/202: connect WiFi then MQTT before scanning starts,
       showing the three-stage boot LED sequence. wifi_Task (already
       created above) drives the actual WiFi connect; this just waits for
       its result before moving on to MQTT. */
    fps_SetLed(FPM_LED_DIAG_1);
    wifi_WaitConnected(APP_WIFI_BOOT_WAIT_TIMEOUT_MS);

    fps_SetLed(FPM_LED_DIAG_2);
    if (mqtt_Init() != RC_SUCCESS)
    {
      ESP_LOGE(TAG, "mqtt_Init failed");
      ceh_Fatal(CEH_ERR_MQTT_RUNTIME); /* confirmed: reuse MQTT_RUNTIME's blink code for boot failure too */
    }

    fps_SetLed(FPM_LED_OP_SUCCESS);

    rc = xTaskCreatePinnedToCore(fps_ScanTask, "scanner",
                                 APP_SCANNER_TASK_STACK, NULL,
                                 APP_SCANNER_TASK_PRIO, NULL, 0);
    if (rc != pdPASS) { ESP_LOGE(TAG, "scanner task create failed"); ceh_Fatal(CEH_ERR_RESOURCE); }

    rc = xTaskCreatePinnedToCore(mqtt_Task, "mqtt",
                                 APP_MQTT_TASK_STACK, NULL,
                                 APP_MQTT_TASK_PRIO, NULL, 1);
    if (rc != pdPASS) { ESP_LOGE(TAG, "mqtt task create failed"); ceh_Fatal(CEH_ERR_RESOURCE); }
  }
  else /* APP_MODE_SETUP: no scanning, no mqtt (SWS-MOD105) */
  {
    /* SWS-MOD109: three-stage Setup-Mode boot LED. Diagnostic State 1
     * (flashing) while the AP/webpage are still coming up; webpage_start_task
     * switches to State 2 (breathing) once the webpage is actually serving
     * but no client has connected yet, then to State 0 (solid) once a
     * client has actually been assigned an IP by our AP's DHCP server —
     * that's the point at which the user is confirmed connected and setup
     * can proceed. */
    fps_SetLed(FPM_LED_DIAG_1);

    webpage_mode_t wp_mode = app_mode_HasAdminFingerprint()
                                ? WEBPAGE_MODE_NORMAL_SETUP
                                : WEBPAGE_MODE_FIRST_RUN; /* SWS-MOD108 */

    rc = xTaskCreatePinnedToCore(webpage_start_task, "webpage_start",
                                 APP_WEBPAGE_START_STACK, (void *)(uintptr_t)wp_mode,
                                 APP_WEBPAGE_START_PRIO, NULL, 1);
    if (rc != pdPASS) { ESP_LOGE(TAG, "webpage start task create failed"); ceh_Fatal(CEH_ERR_RESOURCE); }
  }

  ESP_LOGI(TAG, "all tasks started");
  vTaskDelete(NULL);
}

/******************************************************************************/

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
    case CEH_ERR_WIFI_RUNTIME:
      fps_SetLed(FPM_LED_ERROR_3);
      break;
    case CEH_ERR_MQTT_RUNTIME:
      fps_SetLed(FPM_LED_ERROR_4);
      break;
    case CEH_ERR_WATCHDOG:
      fps_SetLed(FPM_LED_ERROR_5);
      break;
  }
}

/**
 * Waits for AP mode to be up, then starts the Setup-Mode webpage interface.
 * Runs once and deletes itself; the httpd server it starts keeps running
 * out of its own internal task context.
 */
static void webpage_start_task(void *pvParam)
{
  webpage_mode_t wp_mode = (webpage_mode_t)(uintptr_t)pvParam;

  xEventGroupWaitBits(g_sys_events, EVT_WIFI_AP_MODE, pdFALSE, pdFALSE, portMAX_DELAY);

  if (webpage_Init(wp_mode) != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "webpage_Init failed");
    ceh_Fatal(CEH_ERR_RESOURCE);
  }

  /* SWS-MOD109: AP is up and the webpage is now actually serving requests
   * — Setup-Mode is ready, switch from Diagnostic State 1 to State 2 and
   * wait for a client to actually connect and get an IP before going
   * solid. */
  fps_SetLed(FPM_LED_DIAG_2);

  xEventGroupWaitBits(g_sys_events, EVT_WIFI_AP_CLIENT_CONNECTED, pdFALSE, pdFALSE,
                       portMAX_DELAY);

  fps_SetLed(FPM_LED_DIAG_0);

  vTaskDelete(NULL);
}
