/**
 * @file       fps/fps.h
 * @brief      FingerPrint Sensor (FPS) — application-layer wrapper
 *
 *             Wraps the FingerPrintModule library (sub/finger_print_module)
 *             with ESP32 UART callbacks and a FreeRTOS mutex for exclusive
 *             sensor access.
 *
 *             Naming convention: all functions use the fps_ prefix.
 *
 *             Mutex rules:
 *               - fps_scan() and fps_set_led() acquire/release the mutex
 *                 internally (single-operation calls).
 *               - Multi-step sequences (enrollment, bulk management) must call
 *                 fps_lock() / fps_unlock() explicitly and use the raw
 *                 fps_raw_* functions in between.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef FPS_H_
#define FPS_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "basictypes.h"
#include "fpm.h"   /* FingerPrintModule library */

/******************************************************************************/
/*** API Functions — lifecycle                                                */
/******************************************************************************/

/**
 * @brief Initialize the FPM library with ESP32 UART1 callbacks.
 *
 * Creates the FPM mutex and calls fpm_init() with the UART adapter functions.
 * On success, registers the LED backend with ceh.
 *
 * @return RC_SUCCESS, RC_ALREADY_INITIALIZED, or RC_ERROR.
 */
RC_t fps_Init(void);

/******************************************************************************/
/*** API Functions — mutex management (for multi-step sequences)             */
/******************************************************************************/

/**
 * @brief Acquire exclusive FPM sensor access.
 *
 * Blocks up to 500 ms. Call before a sequence of fps_raw_* operations.
 *
 * @return RC_SUCCESS or RC_TIMEOUT.
 */
RC_t fps_Lock(void);

/**
 * @brief Release exclusive FPM sensor access.
 */
RC_t fps_Unlock(void);

/******************************************************************************/
/*** API Functions — single-operation (mutex-protected internally)           */
/******************************************************************************/

/**
 * @brief Scan the currently presented finger and match against the library.
 *
 * Acquires the mutex internally. Intended for use by scanner_task after the
 * FP-sense GPIO interrupt fires.
 * 
 * @warning Protected by Mutex, use fps_Lock() and fps_Unlock()
 *
 * @param[out] o_id     Matched fingerprint slot ID (valid when RC_SUCCESS).
 * @param[out] o_score  Match confidence score (valid when RC_SUCCESS).
 *
 * @return RC_SUCCESS (match), RC_NOT_CONNECTED (no match), or RC_ERROR.
 */
RC_t fps_Scan(uint16_t *o_id, uint16_t *o_score);

/**
 * @brief Set the sensor LED state.
 *
 * Acquires the mutex internally. Safe to call from any task.
 *
 * @param[in] state  Desired LED state.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_SetLed(fpm_led_state_t i_state);

/******************************************************************************/
/*** API Functions — raw (call only while holding the mutex via fps_lock()) */
/******************************************************************************/

/**
 * @brief Capture one enrollment scan.
 *
 * Call twice (scan_num = 1, then 2) before fps_raw_enroll_commit().
 *
 * @param[in] scan_num  1 for the first scan, 2 for the second.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_EnrollScan(uint8_t i_scan_num);

/**
 * @brief Commit the two enrollment scans and store the template.
 *
 * @param[out] o_id  Assigned fingerprint slot ID.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_EnrollCommit(uint16_t *o_id);

/**
 * @brief Write metadata for a fingerprint slot.
 *
 * @param[in] id    Slot ID returned by fps_raw_enroll_commit().
 * @param[in] meta  Metadata to store (uuid, finger_id, function_code).
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_raw_write_meta(uint16_t id, const fpm_fingerprint_meta_t *meta);

/**
 * @brief Read metadata for a fingerprint slot.
 *
 * @param[in]  id    Slot ID.
 * @param[out] meta  Metadata read from storage.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_raw_read_meta(uint16_t id, fpm_fingerprint_meta_t *meta);

/**
 * @brief List all stored fingerprints.
 *
 * @param[out] entries      Output array.
 * @param[in]  max_entries  Capacity of @p entries.
 * @param[out] o_count      Number of entries written.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_raw_list(fpm_fingerprint_entry_t *entries, uint16_t max_entries,
                   uint16_t *o_count);

/**
 * @brief Delete a single fingerprint slot.
 *
 * @param[in] id  Slot ID to delete.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_raw_delete(uint16_t id);

/**
 * @brief Delete all fingerprints stored in the sensor.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_raw_delete_all(void);

/**
 * @brief Delete all fingerprints associated with a UUID.
 *
 * @param[in] uuid  UUID whose fingerprints should be removed.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_raw_delete_by_uuid(uint8_t uuid);

#endif /* FPS_H_ */
