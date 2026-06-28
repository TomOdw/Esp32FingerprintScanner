/**
 * @file       uart.h
 * @brief      UART driver for the FPS sensor (UART1)
 *
 *             Manages UART1 exclusively for the fingerprint sensor. Provides
 *             the read/write callbacks injected into the FPM library via
 *             fpm_init(). UART0 is the IDF console and must not be touched here.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef UART_H_
#define UART_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include <stdint.h>
#include "basictypes.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Initialize UART1 for FPS sensor communication.
 *
 * Configures baud rate, pins, and RX buffer. Must be called after io_Init()
 * and the 500 ms sensor boot delay.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t uart_Init(void);

/**
 * @brief Write bytes to the FPS sensor over UART1.
 *
 * @param[in] i_buf  Data to transmit.
 * @param[in] i_len  Number of bytes to write.
 *
 * @return RC_SUCCESS, RC_INVALID_ARG, or RC_ERROR.
 */
RC_t uart_FpmWrite(const uint8_t *i_buf, uint32_t i_len);

/**
 * @brief Read bytes from the FPS sensor over UART1.
 *
 * Blocks until at least one byte is available or the driver timeout elapses.
 *
 * @param[out] o_buf         Receive buffer.
 * @param[in]  i_max_len     Capacity of @p o_buf.
 * @param[out] o_bytes_read  Actual number of bytes placed in @p o_buf.
 *
 * @return RC_SUCCESS, RC_TIMEOUT, RC_INVALID_ARG, or RC_ERROR.
 */
RC_t uart_FpmRead(uint8_t *o_buf, uint32_t i_max_len, uint32_t *o_bytes_read);

#endif /* UART_H_ */
