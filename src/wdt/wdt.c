/**
 * @file       wdt.c
 * @brief      Cooperative software watchdog, plus hardware TWDT fallback
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
#include "wdt.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"

#include "app_handles.h"
#include "ceh/ceh.h"
#include "nvs/nvs_app.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/** wifi_Task, mqtt_Task, fps_ScanTask — with one slot of headroom. */
#define WDT_MAX_TASKS            4U

#define WDT_MONITOR_TASK_STACK   3072U
#define WDT_MONITOR_TASK_PRIO    2U
#define WDT_MONITOR_TASK_CORE    1

/** How often the monitor task wakes to check registered tasks and the
 *  scheduled-reboot condition. */
#define WDT_MONITOR_PERIOD_MS    1000U

/******************************************************************************/
/*** Types                                                                    */
/******************************************************************************/

typedef struct
{
  bool         in_use;
  TaskHandle_t task_handle;
  TickType_t   last_reset_tick;
  TickType_t   timeout_ticks;
} wdt_entry_t;

/******************************************************************************/
/*** Local variables                                                          */
/******************************************************************************/

static const char *TAG = "wdt";

static wdt_entry_t       s_registry[WDT_MAX_TASKS];
static SemaphoreHandle_t s_registry_mutex = NULL;

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static void wdt_MonitorTask(void *pvParam);
static void check_scheduled_reboot(void);
static int  find_entry(TaskHandle_t i_task);

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t wdt_Init(void)
{
  memset(s_registry, 0, sizeof(s_registry));

  s_registry_mutex = xSemaphoreCreateMutex();
  if (s_registry_mutex == NULL)
  {
    ESP_LOGE(TAG, "xSemaphoreCreateMutex failed");
    return RC_ERROR;
  }

  /* ESP-IDF's hardware TWDT is already auto-initialised by this project's
     sdkconfig (CONFIG_ESP_TASK_WDT_INIT=y) — calling esp_task_wdt_init()
     again here would just return ESP_ERR_INVALID_STATE. wdt_MonitorTask()
     subscribes only itself to it, below. */

  BaseType_t rc = xTaskCreatePinnedToCore(wdt_MonitorTask, "wdt_monitor",
                                          WDT_MONITOR_TASK_STACK, NULL,
                                          WDT_MONITOR_TASK_PRIO, NULL,
                                          WDT_MONITOR_TASK_CORE);
  if (rc != pdPASS)
  {
    ESP_LOGE(TAG, "xTaskCreatePinnedToCore(wdt_monitor) failed");
    return RC_ERROR;
  }

  return RC_SUCCESS;
}

RC_t wdt_RegisterTask(uint32_t i_timeout_ms)
{
  if (s_registry_mutex == NULL)
  {
    return RC_ERROR;
  }

  RC_t rc = RC_ERROR;

  xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
  for (uint32_t i = 0U; i < WDT_MAX_TASKS; i++)
  {
    if (!s_registry[i].in_use)
    {
      s_registry[i].in_use          = true;
      s_registry[i].task_handle     = xTaskGetCurrentTaskHandle();
      s_registry[i].last_reset_tick = xTaskGetTickCount();
      s_registry[i].timeout_ticks   = pdMS_TO_TICKS(i_timeout_ms);
      rc = RC_SUCCESS;
      break;
    }
  }
  xSemaphoreGive(s_registry_mutex);

  if (rc != RC_SUCCESS)
  {
    ESP_LOGE(TAG, "registry full");
  }

  return rc;
}

RC_t wdt_Reset(void)
{
  if (s_registry_mutex == NULL)
  {
    return RC_ERROR;
  }

  RC_t rc = RC_ERROR;

  xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
  int idx = find_entry(xTaskGetCurrentTaskHandle());
  if (idx >= 0)
  {
    s_registry[idx].last_reset_tick = xTaskGetTickCount();
    rc = RC_SUCCESS;
  }
  xSemaphoreGive(s_registry_mutex);

  return rc;
}

RC_t wdt_UnregisterTask(void)
{
  if (s_registry_mutex == NULL)
  {
    return RC_ERROR;
  }

  RC_t rc = RC_ERROR;

  xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
  int idx = find_entry(xTaskGetCurrentTaskHandle());
  if (idx >= 0)
  {
    s_registry[idx].in_use = false;
    rc = RC_SUCCESS;
  }
  xSemaphoreGive(s_registry_mutex);

  return rc;
}

/******************************************************************************/
/*** Local function implementation                                            */
/******************************************************************************/

/** Caller must hold s_registry_mutex. */
static int find_entry(TaskHandle_t i_task)
{
  for (uint32_t i = 0U; i < WDT_MAX_TASKS; i++)
  {
    if (s_registry[i].in_use && (s_registry[i].task_handle == i_task))
    {
      return (int)i;
    }
  }
  return -1;
}

static void wdt_MonitorTask(void *pvParam)
{
  (void)pvParam;

  /* Subscribe only this task to the hardware TWDT — the absolute fallback
     for the one case the software layer above cannot catch: itself
     freezing. Application tasks are watched exclusively by the software
     registry, not individually added to TWDT. */
  esp_task_wdt_add(NULL);

  const TickType_t period = pdMS_TO_TICKS(WDT_MONITOR_PERIOD_MS);

  for (;;)
  {
    vTaskDelay(period);
    esp_task_wdt_reset();

    TickType_t now = xTaskGetTickCount();
    bool       tripped = false;

    xSemaphoreTake(s_registry_mutex, portMAX_DELAY);
    for (uint32_t i = 0U; i < WDT_MAX_TASKS; i++)
    {
      if (s_registry[i].in_use &&
          ((now - s_registry[i].last_reset_tick) > s_registry[i].timeout_ticks))
      {
        tripped = true;
        break;
      }
    }
    xSemaphoreGive(s_registry_mutex);

    if (tripped)
    {
      ESP_LOGE(TAG, "watchdog trip: a registered task exceeded its timeout");
      /* Unsubscribe from the hardware TWDT before entering ceh_Fatal()'s
         graceful blink-then-reboot sequence (up to 30s, SWS-CEH004): that
         sequence already guarantees a reboot at the end, satisfying the
         same "never hang forever" property the hardware fallback exists
         for, so continuing to require esp_task_wdt_reset() during it
         would just trip the hardware TWDT's own (much shorter) timeout
         mid-blink and turn a graceful reboot into a hard abort — this
         task is not frozen, it's deliberately here. */
      esp_task_wdt_delete(NULL);
      ceh_Fatal(CEH_ERR_WATCHDOG); /* noreturn */
    }

    check_scheduled_reboot();
  }
}

/**
 * @brief SWS-WDT002: plain, unconditional reboot once the NVS-configured
 * scheduled-reboot interval has elapsed since boot. 0 minutes = disabled.
 * Not routed through the Central Error Handler — this isn't an error.
 */
static void check_scheduled_reboot(void)
{
  uint32_t minutes = 0U;
  if ((nvs_GeneralGetRebootMinutes(&minutes) != RC_SUCCESS) || (minutes == 0U))
  {
    return;
  }

  int64_t elapsed_us  = esp_timer_get_time();
  int64_t target_us   = (int64_t)minutes * 60LL * 1000000LL;

  if (elapsed_us >= target_us)
  {
    ESP_LOGW(TAG, "scheduled reboot interval elapsed (%u min); restarting", (unsigned)minutes);

    /* esp_restart() tears down WiFi mid-flight, which would otherwise
       fire the normal runtime-disconnect handlers (wifi.c/mqtt.c) and log
       a spurious non-fatal CEH entry for what is actually expected,
       deliberate behaviour — see the same fix in fps.c's SWS-FPM204
       admin-override reboot. */
    xEventGroupClearBits(g_sys_events, EVT_WIFI_CONNECTED | EVT_MQTT_CONNECTED);
    esp_restart();
  }
}
