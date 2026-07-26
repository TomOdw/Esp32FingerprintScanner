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
#include "esp_log.h"
#include "app_handles.h"

/******************************************************************************/
/*** Variables                                                                */
/******************************************************************************/

static const char *TAG = "fps";

/**
 * @brief Last LED state actually sent to the sensor.
 *
 * The sensor keeps animating a breathe/flash/blink mode on its own once
 * commanded — it does not need (or want) the command repeated. Callers
 * like scanner_task and the Setup-Mode enroll wizard call fps_SetLed()
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

