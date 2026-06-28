/**
 * @file       io.h
 * @brief      GPIO management
 *
 *             Manages two GPIO pins:
 *               - Sensor power pin (output): drives sensor VCC via transistor.
 *               - FP sense pin (input/interrupt): R503 touch-detect output.
 *                 The ISR gives g_fp_sense_sem to wake scanner_task.
 *
 *             io_Init() must be called before uart_Init() so that the sensor
 *             is powered before any UART communication begins.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef IO_H_
#define IO_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "basictypes.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Initialize GPIO.
 *
 * - Drives the sensor power pin HIGH to power up the sensor.
 * - Configures the FP sense pin as a rising-edge interrupt input.
 * - Registers the ISR that gives g_fp_sense_sem.
 *
 * g_fp_sense_sem must be created by app_main before calling this function.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t io_Init(void);

#endif /* IO_H_ */
