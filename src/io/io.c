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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "app_handles.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/**
 * @brief R503 touch-detect output (SENSE/TOUCH pin), active HIGH.
 *
 * Configured as a rising-edge interrupt input. The ISR gives g_fp_sense_sem
 * to wake scanner_task.
 */
#define IO_PIN_FP_SENSE    34

/******************************************************************************/
/*** Local function declaration                                               */
/******************************************************************************/

static void fp_sense_isr(void *arg);

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t io_Init(void)
{
  /* FP sense pin: input, rising-edge interrupt.
   * GPIO34 has no internal pull resistors — an external pull-down on the
   * sense line is required to hold the pin LOW when no finger is present. */
  const gpio_config_t sense_cfg = {
    .pin_bit_mask = (1ULL << IO_PIN_FP_SENSE),
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_POSEDGE,
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
