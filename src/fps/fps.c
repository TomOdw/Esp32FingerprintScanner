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
#include "esp_log.h"
#include "app_handles.h"

/******************************************************************************/
/*** Variables                                                                */
/******************************************************************************/

static const char *TAG = "fps";

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
  return (fpm_set_led(i_state) == FPM_RC_OK) ? RC_SUCCESS : RC_ERROR;
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
