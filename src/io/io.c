/**
 * @file       io.c
 * @brief      GPIO management
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
#include "io/io.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "app_handles.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/**
 * @brief FP sense input, active LOW, wired directly to the R503's own
 *        TOUCH output (no external buffer stage).
 *
 * Verified with an oscilloscope: idle reads 3.3V, a finger pulls the line
 * to 0V. An external transistor buffer stage was tried at one point,
 * based on disturbances observed while this pin was actively polled — but
 * those disturbances were later traced to a combination of an
 * (since-removed) busy-wait polling loop and a floating-pin artifact that
 * only appeared while the wire was disconnected for testing, not to the
 * direct connection itself. The buffer was removed once the real causes
 * (busy-wait polling, and separately, a too-short UART read timeout for
 * search responses — see uart.c) were fixed. Configured as a falling-edge
 * interrupt input. The ISR gives g_fp_sense_sem to wake fps_ScanTask.
 */
#define IO_PIN_FP_SENSE    26

/** Interval between polls in wait_for_level(). Each poll is a plain
 *  vTaskDelay() sleep, not a busy-wait, so this stays essentially idle
 *  between reads. */
#define IO_FP_SENSE_POLL_MS   20U

/**
 * @brief wait_for_level()'s debounce: the pin must read the wanted level
 *        on this many consecutive polls before it's trusted — i.e. it
 *        must hold for at least (IO_FP_SENSE_STABLE_COUNT - 1) *
 *        IO_FP_SENSE_POLL_MS. Rejects brief contact bounce without needing
 *        to be any longer than that: the level is clean and steady in
 *        both directions with no finger movement, on real hardware.
 */
#define IO_FP_SENSE_STABLE_COUNT   3U   /* ~60ms */

/******************************************************************************/
/*** Variables                                                                */
/******************************************************************************/

static const char *TAG = "io";

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static void fp_sense_isr(void *arg);
static bool read_raw_present(void);
static RC_t wait_for_level(bool i_want_present, TickType_t i_deadline);

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t io_Init(void)
{
  /* FP sense pin: input, falling-edge interrupt (active LOW — see
   * IO_PIN_FP_SENSE above). No internal pull needed or wanted: the sensor
   * drives this pin push-pull in both states, and no external pull
   * resistor should be added either. */
  const gpio_config_t sense_cfg = {
    .pin_bit_mask = (1ULL << IO_PIN_FP_SENSE),
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_NEGEDGE,
  };
  if (gpio_config(&sense_cfg) != ESP_OK)
  {
    return RC_ERROR;
  }

  /* Install ISR service; ignore ESP_ERR_INVALID_STATE if already installed */
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    return RC_ERROR;
  }

  if (gpio_isr_handler_add(IO_PIN_FP_SENSE, fp_sense_isr, NULL) != ESP_OK)
  {
    return RC_ERROR;
  }

  return RC_SUCCESS;
}

bool io_FpSensePresent(void)
{
  return read_raw_present();
}

RC_t io_WaitFingerPresent(uint32_t i_timeout_ms)
{
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(i_timeout_ms);

  /* Require a genuine edge, not just a level read. Confirmed on real
   * hardware (independently, via the sensor vendor's own PC test tool —
   * not just this firmware): performing a scan/capture drives this pin
   * the same way a real touch does while it's in progress, and can leave
   * it reading "present" afterward with no finger there at all. A plain
   * level check can't tell that apart from a genuine touch. Waiting for
   * a confirmed "not present" baseline first, then a fresh transition to
   * "present", ensures this only succeeds on an actual new touch — never
   * on a stale level left over from the sensor's own internal activity.
   * This applies uniformly, including the very first capture of a
   * session: if a finger is already resting on the sensor before the
   * caller starts waiting, it still has to be lifted and placed again. */
  if (wait_for_level(false, deadline) != RC_SUCCESS)
  {
    return RC_TIMEOUT;
  }
  return wait_for_level(true, deadline);
}

/******************************************************************************/
/*** Local function implementation                                            */
/******************************************************************************/

static void IRAM_ATTR fp_sense_isr(void *arg)
{
  (void)arg;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(g_fp_sense_sem, &woken);
  portYIELD_FROM_ISR(woken);
}

/**
 * @brief Single, unfiltered read of the sense pin level.
 *
 * Logs every raw level change as it happens — this is the ground truth of
 * what the pin is actually doing; wait_for_level() layers debounce on top
 * of repeated calls to this, it does no filtering itself. Logged on change
 * only (not every call) to stay readable; a genuinely bouncing/noisy pin
 * will still show up as a burst of these.
 */
static bool read_raw_present(void)
{
  static bool s_last_logged  = false;
  static bool s_have_logged  = false;

  bool present = (gpio_get_level(IO_PIN_FP_SENSE) == 0); /* active LOW */

  if (!s_have_logged || present != s_last_logged)
  {
    ESP_LOGI(TAG, "raw sense pin level: %s", present ? "present" : "not present");
    s_last_logged = present;
    s_have_logged = true;
  }

  return present;
}

/**
 * @brief Poll the sense pin until it reads i_want_present consistently for
 *        IO_FP_SENSE_STABLE_COUNT consecutive polls, or i_deadline passes.
 *
 * Any read that doesn't match resets the streak — a single stray reading
 * doesn't count.
 */
static RC_t wait_for_level(bool i_want_present, TickType_t i_deadline)
{
  uint32_t stable_count = 0;

  for (;;)
  {
    if (read_raw_present() == i_want_present)
    {
      stable_count++;
      if (stable_count >= IO_FP_SENSE_STABLE_COUNT)
      {
        return RC_SUCCESS;
      }
    }
    else
    {
      stable_count = 0;
    }

    if (xTaskGetTickCount() >= i_deadline)
    {
      ESP_LOGI(TAG, "wait_for_level: timed out waiting for %s",
               i_want_present ? "present" : "not present");
      return RC_TIMEOUT;
    }
    vTaskDelay(pdMS_TO_TICKS(IO_FP_SENSE_POLL_MS));
  }
}
