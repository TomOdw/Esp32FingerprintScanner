/**
 * @file       uart.c
 * @brief      UART driver for the FPS sensor (UART1)
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
#include "uart/uart.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/** Baud rate for the FPS sensor (R503 default: 57600) */
#define UART_FPM_BAUD_RATE  57600U

/** UART1 TX pin — connects to sensor RX */
#define UART_FPM_TX_PIN     17

/** UART1 RX pin — connects to sensor TX */
#define UART_FPM_RX_PIN     16

#define UART_FPM_NUM              UART_NUM_2
#define UART_FPM_RX_BUF           1024U
#define UART_FPM_READ_TIMEOUT_MS  500U

/******************************************************************************/
/*** API function implementation                                              */
/******************************************************************************/

RC_t uart_Init(void)
{
  const uart_config_t cfg = {
    .baud_rate  = UART_FPM_BAUD_RATE,
    .data_bits  = UART_DATA_8_BITS,
    .parity     = UART_PARITY_DISABLE,
    .stop_bits  = UART_STOP_BITS_1,
    .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
  };

  if (uart_param_config(UART_FPM_NUM, &cfg) != ESP_OK)
  {
    return RC_ERROR;
  }

  if (uart_set_pin(UART_FPM_NUM,
                   UART_FPM_TX_PIN, UART_FPM_RX_PIN,
                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
  {
    return RC_ERROR;
  }

  if (uart_driver_install(UART_FPM_NUM, UART_FPM_RX_BUF, 0, 0, NULL, 0) != ESP_OK)
  {
    return RC_ERROR;
  }

  return RC_SUCCESS;
}

RC_t uart_FpmWrite(const uint8_t *i_buf, uint32_t i_len)
{
  if (i_buf == NULL || i_len == 0U)
  {
    return RC_INVALID_ARG;
  }

  int written = uart_write_bytes(UART_FPM_NUM, (const char *)i_buf, i_len);
  return ((uint32_t)written == i_len) ? RC_SUCCESS : RC_ERROR;
}

RC_t uart_FpmRead(uint8_t *o_buf, uint32_t i_max_len, uint32_t *o_bytes_read)
{
  if (o_buf == NULL || o_bytes_read == NULL)
  {
    return RC_INVALID_ARG;
  }

  /* Loop until all requested bytes arrive or no byte arrives within the
   * timeout window. The library checks readByteCnt == expected and treats
   * any short read as a communication error, so partial reads must not be
   * returned. */
  uint32_t total = 0;
  while (total < i_max_len)
  {
    int n = uart_read_bytes(UART_FPM_NUM,
                            o_buf + total,
                            i_max_len - total,
                            pdMS_TO_TICKS(UART_FPM_READ_TIMEOUT_MS));
    if (n < 0)
    {
      return RC_ERROR;
    }
    if (n == 0)
    {
      break; /* timeout with no new bytes — let caller handle short read */
    }
    total += (uint32_t)n;
  }

  *o_bytes_read = total;
  return RC_SUCCESS;
}
