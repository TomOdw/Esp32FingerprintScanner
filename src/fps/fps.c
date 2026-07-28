/**
 * @file       fps/fps.c
 * @brief      FingerPrint Sensor (FPS) — application-layer wrapper
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
#include "fps/fps.h"
#include "uart/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "app_handles.h"

#include "io/io.h"
#include "wdt/wdt.h"
#include "nvs/nvs_app.h"
#include "mqtt/mqtt.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/** Generous enough to cover a full scan session: io_WaitFingerPresent's
 *  10s follow-up window plus scan/result-display time. */
#define FPS_SCAN_TASK_WDT_TIMEOUT_MS  15000U

/** How long a scan result LED (SCAN_SUCCESS/SCAN_FAILED) stays shown
 *  before the task moves on (SWS-FPM202/104) — matches webpage.c's
 *  RESULT_DISPLAY_MS for a consistent feel between Setup- and Normal-Mode. */
#define FPS_RESULT_DISPLAY_MS         2000U

/** SWS-FPM202: how long fps_ScanTask() polls for a same-session follow-up
 *  scan before reverting to idle. */
#define FPS_POST_SCAN_TIMEOUT_MS      10000U

/******************************************************************************/
/*** Variables                                                                */
/******************************************************************************/

static const char *TAG = "fps";

/**
 * @brief Last LED state actually sent to the sensor.
 *
 * The sensor keeps animating a breathe/flash/blink mode on its own once
 * commanded — it does not need (or want) the command repeated. Callers
 * like fps_ScanTask and the Setup-Mode enroll wizard call fps_SetLed()
 * on every loop iteration/request regardless of whether the state
 * actually changed; without this cache, each redundant call restarts the
 * animation from scratch, which is visible as a stutter (e.g. the
 * breathing-blue "waiting for finger" cue glitching every time an
 * in-progress HTTP request is merely retried).
 */
static fpm_led_state_t s_current_led = FPM_LED_OFF;

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static fpm_RC_t uart_write_adapter(const uint8_t *const i_buf,
                                   const uint32_t i_buf_size);
static fpm_RC_t uart_read_adapter(uint8_t *const o_buf,
                                  const uint32_t i_buf_size,
                                  uint32_t *const o_bytes_read);

/******************************************************************************/
/*** API function implementation — lifecycle                                  */
/******************************************************************************/

RC_t fps_Init(void)
{
  static const fpm_cfg_t s_cfg = {
    .uart_write_bytes = uart_write_adapter,
    .uart_read_bytes  = uart_read_adapter,
    .logging_func     = NULL,
    .sensor_cfg       = NULL,
  };
  fpm_RC_t rc = FPM_RC_ERROR;
  rc = fpm_init(&s_cfg);

  switch (rc)
  {
  case FPM_RC_OK:
    return RC_SUCCESS;
    break;

  default:
    ESP_LOGE(TAG, "fps_Init: Error: %u", rc);
    return RC_ERROR;
    break;
  }
}

/******************************************************************************/
/*** API function implementation — mutex management                          */
/******************************************************************************/

RC_t fps_Lock(void)
{
  if (xSemaphoreTake(g_fpm_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
  {
    return RC_TIMEOUT;
  }
  return RC_SUCCESS;
}

RC_t fps_Unlock(void)
{
  xSemaphoreGive(g_fpm_mutex);
  return RC_SUCCESS;
}

/******************************************************************************/
/*** API function implementation — single-operation                          */
/******************************************************************************/

RC_t fps_Scan(uint16_t *o_id, uint16_t *o_score)
{
  if (o_id == NULL || o_score == NULL)
  {
    return RC_INVALID_ARG;
  }

  switch (fpm_scan_fingerprint(o_id, o_score))
  {
    case FPM_RC_OK:       return RC_SUCCESS;
    case FPM_RC_NO_MATCH: return RC_NO_MATCH;
    default:              return RC_ERROR;
  }
}

RC_t fps_SetLed(fpm_led_state_t i_state)
{
  if (i_state == s_current_led)
  {
    return RC_SUCCESS;
  }

  RC_t rc = (fpm_set_led(i_state) == FPM_RC_OK) ? RC_SUCCESS : RC_ERROR;
  if (rc == RC_SUCCESS)
  {
    s_current_led = i_state;
  }
  return rc;
}

/******************************************************************************/
/*** API function implementation — raw (caller holds mutex)                  */
/******************************************************************************/

RC_t fps_EnrollScan(uint8_t i_scan_num)
{
  return (fpm_enroll_scan(i_scan_num) == FPM_RC_OK) ? RC_SUCCESS : RC_ERROR;
}

RC_t fps_EnrollCommit(uint16_t *o_id)
{
  return (fpm_enroll_commit(o_id) == FPM_RC_OK) ? RC_SUCCESS : RC_ERROR;
}

RC_t fps_raw_write_meta(uint16_t id, const fpm_fingerprint_meta_t *meta)
{
  return (fpm_write_fingerprint_meta(id, meta) == FPM_RC_OK) ? RC_SUCCESS : RC_ERROR;
}

RC_t fps_raw_read_meta(uint16_t id, fpm_fingerprint_meta_t *meta)
{
  return (fpm_read_fingerprint_meta(id, meta) == FPM_RC_OK) ? RC_SUCCESS : RC_ERROR;
}

RC_t fps_raw_list(fpm_fingerprint_entry_t *entries, uint16_t max_entries,
                  uint16_t *o_count)
{
  return (fpm_list_fingerprints(entries, max_entries, o_count) == FPM_RC_OK)
           ? RC_SUCCESS : RC_ERROR;
}

RC_t fps_raw_delete(uint16_t id)
{
  return (fpm_delete_fingerprint(id) == FPM_RC_OK) ? RC_SUCCESS : RC_ERROR;
}

RC_t fps_raw_delete_all(void)
{
  return (fpm_delete_all_fingerprints() == FPM_RC_OK) ? RC_SUCCESS : RC_ERROR;
}

RC_t fps_raw_delete_by_uuid(uint8_t uuid)
{
  return (fpm_delete_by_uuid(uuid) == FPM_RC_OK) ? RC_SUCCESS : RC_ERROR;
}

/******************************************************************************/
/*** API function implementation — bounded-poll                              */
/******************************************************************************/

RC_t fps_EnrollStep(uint8_t i_scan_num, uint32_t i_timeout_ms)
{
  if (i_scan_num != 1U && i_scan_num != 2U)
  {
    return RC_INVALID_ARG;
  }

  /* Purely a capture-retry primitive, matching fps_ScanStep()'s shape:
   * presence-detection is the caller's job (webpage.c), not this
   * function's — that lets the caller show a genuine "scanning" LED state
   * for exactly the window this call is actually active, rather than
   * folding the presence-wait in here where the caller can't distinguish
   * "still waiting for a finger" from "actively capturing". */
  if (fps_Lock() != RC_SUCCESS)
  {
    return RC_TIMEOUT;
  }

  /* Even with presence already confirmed by the caller, the capture
   * itself can still fail transiently (image not yet stable, sensor still
   * busy from a prior op, brief contact noise) — retry it for the full
   * time budget instead of bailing out on the first attempt. Without this
   * retry, a single transient failure returns RC_TIMEOUT almost
   * instantly, the frontend's retry loop re-calls immediately with no
   * delay, and the resulting flood of requests visibly flickers the LED
   * between AWAITING_FINGER/ERROR_1/DIAG_0 on every round trip. */
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(i_timeout_ms);
  RC_t rc;
  do
  {
    rc = fps_EnrollScan(i_scan_num);
    if (rc == RC_SUCCESS)
    {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(FPS_POLL_INTERVAL_MS));
  } while (xTaskGetTickCount() < deadline);

  fps_Unlock();

  return (rc == RC_SUCCESS) ? RC_SUCCESS : RC_TIMEOUT;
}

RC_t fps_EnrollCommitAndTag(const fpm_fingerprint_meta_t *meta, uint16_t *o_id)
{
  if (meta == NULL || o_id == NULL)
  {
    return RC_INVALID_ARG;
  }

  if (fps_Lock() != RC_SUCCESS)
  {
    return RC_TIMEOUT;
  }

  RC_t rc = fps_EnrollCommit(o_id);
  if (rc == RC_SUCCESS)
  {
    rc = fps_raw_write_meta(*o_id, meta);
  }

  fps_Unlock();
  return rc;
}

RC_t fps_ScanStep(uint32_t i_timeout_ms, uint16_t *o_id, uint16_t *o_score)
{
  if (o_id == NULL || o_score == NULL)
  {
    return RC_INVALID_ARG;
  }

  if (fps_Lock() != RC_SUCCESS)
  {
    return RC_TIMEOUT;
  }

  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(i_timeout_ms);
  RC_t rc;
  uint32_t attempt = 0;
  uint32_t no_match_count = 0;
  do
  {
    attempt++;
    rc = fps_Scan(o_id, o_score);
    ESP_LOGI(TAG, "fps_ScanStep attempt %u: fps_Scan rc=%d id=%u score=%u",
             (unsigned)attempt, (int)rc, (unsigned)*o_id, (unsigned)*o_score);
    if (rc == RC_SUCCESS)
    {
      break;
    }
    if (rc == RC_NO_MATCH)
    {
      /* Don't act on the first NO_MATCH — see FPS_SCAN_NO_MATCH_CONFIRM. */
      no_match_count++;
      if (no_match_count >= FPS_SCAN_NO_MATCH_CONFIRM)
      {
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(FPS_POLL_INTERVAL_MS));
  } while (xTaskGetTickCount() < deadline);

  fps_Unlock();
  return (rc == RC_SUCCESS || rc == RC_NO_MATCH) ? rc : RC_TIMEOUT;
}

/******************************************************************************/
/*** API function implementation — Normal-Mode scan task                     */
/******************************************************************************/

void fps_ScanTask(void *pvParam)
{
  (void)pvParam;
  wdt_RegisterTask(FPS_SCAN_TASK_WDT_TIMEOUT_MS);

  for (;;)
  {
    /* Normal-Mode baseline (SWS-FPM202): idle, off, blocked on the
       FP-sense GPIO interrupt's wake signal — no CPU spent while idle.
       This can legitimately last minutes or hours between scans (nobody
       touching the sensor is not a hang), so wait in short bounded
       chunks and pet the watchdog between them rather than blocking on
       the semaphore indefinitely — an indefinite block would starve
       wdt_Reset() and trip the software watchdog the very first time no
       finger is presented within FPS_SCAN_TASK_WDT_TIMEOUT_MS of the
       previous reset. */
    fps_SetLed(FPM_LED_OFF);

    /* A scan/capture attempt can itself briefly drive the sense pin the
       same way a real touch does (see io_WaitFingerPresent()'s own
       comment in io.c) — so by the time a session ends, g_fp_sense_sem
       may already be sitting "given" from that residual activity, or
       from a real touch that arrived while this task was still busy
       handling the previous one, not from a genuine new touch. Trusting
       that stale give here would immediately re-trigger another scan
       attempt against an empty sensor, which itself re-toggles the pin
       and re-arms the semaphore again — an infinite loop that never
       actually reaches idle. Drain any such pending give before
       committing to the idle wait below. */
    xSemaphoreTake(g_fp_sense_sem, 0);

    while (xSemaphoreTake(g_fp_sense_sem, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
      wdt_Reset();
    }
    wdt_Reset();

    bool session_active = true;
    while (session_active)
    {
      /* Attempt the scan directly — the wake itself already means a
         finger just made contact, so showing "awaiting finger" first
         would just be a visible glitch for something that already
         happened (SWS-FPM202). */
      uint16_t fp_id   = 0U;
      uint16_t score   = 0U;
      RC_t     rc      = RC_ERROR;
      bool     matched = false;

      if (fps_Lock() == RC_SUCCESS)
      {
        rc = fps_Scan(&fp_id, &score);
        fps_Unlock();
      }

      if (rc == RC_SUCCESS)
      {
        matched = true;
        fpm_fingerprint_meta_t meta = {0};
        fps_Lock();
        fps_raw_read_meta(fp_id, &meta);
        fps_Unlock();

        if (meta.finger_id == FPS_ADMIN_FINGER_ID)
        {
          /* SWS-FPM204: admin/master fingerprint scanned during Normal-Mode
             operation — re-enter Setup-Mode instead of publishing a scan
             result (SWS-FPM203/SWS-MQT04 do not apply to this match). Not
             routed through the Central Error Handler: this isn't an error
             condition (mirrors SWS-WDT002's scheduled reboot). */
          ESP_LOGI(TAG, "admin fingerprint scanned; re-entering Setup-Mode");
          fps_SetLed(FPM_LED_OP_SUCCESS);
          vTaskDelay(pdMS_TO_TICKS(FPS_RESULT_DISPLAY_MS));
          nvs_GeneralSetSetupFlag(true);

          /* esp_restart() below tears down WiFi mid-flight, which fires
             the normal WIFI_EVENT_STA_DISCONNECTED / MQTT_EVENT_DISCONNECTED
             handlers — those can't otherwise tell "intentional reboot in
             progress" apart from a real connectivity loss, and would log
             a spurious non-fatal CEH entry (SWS-CEH003) for what is
             actually expected, deliberate behaviour. Both handlers gate
             their logging on these exact bits, so clearing them first is
             enough to make them correctly skip it. */
          xEventGroupClearBits(g_sys_events, EVT_WIFI_CONNECTED | EVT_MQTT_CONNECTED);
          esp_restart();
        }

        mqtt_scan_event_t event = {
          .fp_id         = fp_id,
          .score         = score,
          .uuid          = meta.uuid,
          .finger_id     = meta.finger_id,
          .function_code = meta.function_code,
          .matched       = true,
          .timestamp_us  = esp_timer_get_time(),
        };
        nvs_UserGetName(meta.uuid, event.name, sizeof(event.name));
        xQueueSend(g_scan_queue, &event, 0);

        fps_SetLed(FPM_LED_SCAN_SUCCESS);
      }
      else if (rc == RC_NO_MATCH)
      {
        mqtt_scan_event_t event = {
          .matched      = false,
          .timestamp_us = esp_timer_get_time(),
        };
        xQueueSend(g_scan_queue, &event, 0);

        fps_SetLed(FPM_LED_SCAN_FAILED);
      }
      else
      {
        /* Transient scan/sensor hiccup — display only, not a CEH
           condition (not a WiFi/MQTT/resource/watchdog failure). */
        ESP_LOGW(TAG, "scan error: %d", rc);
        fps_SetLed(FPM_LED_ERROR_1);
      }

      vTaskDelay(pdMS_TO_TICKS(FPS_RESULT_DISPLAY_MS));
      wdt_Reset();

      if (matched)
      {
        /* SWS-FPM202: a match is a terminal event for the session — done
           regardless of whether the MQTT publish itself happened or was
           skipped (e.g. an unconfigured function code) — so there is no
           reason to wait around for a possible follow-up scan. Go
           straight back to idle. */
        session_active = false;
      }
      else
      {
        /* No match / scan error: resume "awaiting finger" and actively
           poll for a possible retry in the same session. */
        fps_SetLed(FPM_LED_AWAITING_FINGER);
        RC_t wait_rc = io_WaitFingerPresent(FPS_POST_SCAN_TIMEOUT_MS);
        wdt_Reset();

        if (wait_rc != RC_SUCCESS)
        {
          session_active = false;
        }
      }
    }
  }
}

/******************************************************************************/
/*** Local function implementation                                            */
/******************************************************************************/

static fpm_RC_t uart_write_adapter(const uint8_t *const i_buf,
                                   const uint32_t i_buf_size)
{
  return (uart_FpmWrite(i_buf, i_buf_size) == RC_SUCCESS)
           ? FPM_RC_OK : FPM_RC_ERR_COMMUNICATION;
}

static fpm_RC_t uart_read_adapter(uint8_t *const o_buf,
                                  const uint32_t i_buf_size,
                                  uint32_t *const o_bytes_read)
{
  return (uart_FpmRead(o_buf, i_buf_size, o_bytes_read) == RC_SUCCESS)
           ? FPM_RC_OK : FPM_RC_ERR_COMMUNICATION;
}

