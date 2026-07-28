/**
 * @file       io.h
 * @brief      GPIO management
 *
 *             Manages two GPIO pins:
 *               - Sensor power pin (output): drives sensor VCC via transistor.
 *               - FP sense pin (input/interrupt): R503 touch-detect output.
 *                 The ISR gives g_fp_sense_sem to wake fps_ScanTask.
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
 * - Configures the FP sense pin as a falling-edge interrupt input
 *   (active LOW, verified by oscilloscope — see io.c).
 * - Registers the ISR that gives g_fp_sense_sem.
 *
 * g_fp_sense_sem must be created by app_main before calling this function.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t io_Init(void);

/**
 * @brief Read the current, instantaneous state of the FP sense pin.
 *
 * Unlike g_fp_sense_sem (an edge-triggered, consume-once signal owned by
 * fps_ScanTask in Normal-Mode), this is a plain level read safe to poll
 * from any task at any time. This is a single raw read with no filtering
 * or debounce of its own — a lone call can reflect brief contact bounce or
 * noise. For a debounced result, use io_WaitFingerPresent() instead, which
 * requires the level to hold steady across multiple polls before trusting
 * it.
 *
 * @return true if a finger is currently detected, false otherwise.
 */
bool io_FpSensePresent(void);

/**
 * @brief Bounded-poll wait for a finger to be freshly placed.
 *
 * Requires a genuine edge, not just a level: first waits for a confirmed
 * "not present" baseline, then a fresh debounced transition to "present"
 * (see IO_FP_SENSE_STABLE_COUNT in io.c). Confirmed on real hardware
 * (independently, via the sensor vendor's own PC test tool) that
 * performing a scan/capture can leave this pin reading "present" for a
 * while afterward with no finger there — a plain level check can't
 * distinguish that from a genuine touch, so this function never trusts a
 * mere level, only an actual transition. There is deliberately no
 * corresponding "wait for lift" function: the same real-hardware testing
 * showed the sensor cannot reliably report a lift after a scan at all,
 * only a fresh touch, so callers needing "the user lifted and placed
 * again" rely on this function alone for that — every call requires a
 * fresh touch by construction, even at the very start of a session.
 *
 * Each poll is a plain vTaskDelay() sleep, not a busy-wait — this stays
 * essentially idle between reads. Does not touch the sensor over UART.
 *
 * @param[in] i_timeout_ms Overall time budget to wait.
 *
 * @return RC_SUCCESS (finger freshly present) or RC_TIMEOUT.
 */
RC_t io_WaitFingerPresent(uint32_t i_timeout_ms);

#endif /* IO_H_ */
