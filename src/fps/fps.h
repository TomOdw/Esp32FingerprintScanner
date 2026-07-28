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
/*** Defines                                                                  */
/******************************************************************************/

/** Poll interval used internally by fps_EnrollStep()/fps_ScanStep(). */
#define FPS_POLL_INTERVAL_MS  300U

/**
 * @brief Consecutive NO_MATCH results fps_ScanStep() requires before
 *        treating the outcome as a definitive non-match.
 *
 * Each attempt is a fresh, independent image capture + 1:N search, and
 * captures of the very same finger vary in quality from one instant to the
 * next (light contact, slight shift, moisture) — a single NO_MATCH does not
 * reliably mean "wrong finger", only "this particular frame didn't match".
 * Requiring a short run of fresh, real NO_MATCH results (rather than acting
 * on the first one) filters out that per-frame variance without pretending
 * to detect anything that isn't really being measured.
 */
#define FPS_SCAN_NO_MATCH_CONFIRM  3U

/** Capacity needed for a full fps_raw_list() dump (0..FPM_META_MAX_ID). */
#define FPS_MAX_TEMPLATES     ((uint16_t)(FPM_META_MAX_ID + 1))

/**
 * @brief Reserved metadata values identifying the admin/master fingerprint
 *        (SWS-WP201). Deliberately outside the regular user range: UUID 0
 *        is "invalid/empty" in the users NVS table (SWS-NVS003) and
 *        finger_id 15 is outside the 0-9 range used for real fingers
 *        (SWS-WP104.3), so the admin fingerprint is never confused with a
 *        regular enrolled user.
 */
#define FPS_ADMIN_UUID           0U
#define FPS_ADMIN_FINGER_ID     15U
#define FPS_ADMIN_FUNCTION_CODE  0U

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
 * Acquires the mutex internally. Intended for use by fps_ScanTask after the
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
 * Acquires the mutex internally. Safe to call from any task. Idempotent:
 * if @p i_state is already the last state set, this is a no-op — the
 * sensor animates breathe/flash/blink modes on its own once commanded, so
 * callers may call this unconditionally on every loop iteration/request
 * without restarting/glitching an in-progress animation.
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

/******************************************************************************/
/*** API Functions — bounded-poll (for request/response callers, e.g. HTTP)  */
/******************************************************************************/

/**
 * @brief Bounded-retry one enrollment scan (SWS-FPM102).
 *
 * A pure capture-retry primitive, matching fps_ScanStep()'s shape: it does
 * NOT wait for a finger to be present first, does not touch the LED, and
 * does not wait for the finger to be lifted afterward. Presence-detection
 * (io_WaitFingerPresent(), in io.c) and all LED sequencing are the
 * caller's responsibility (webpage.c) — this separation lets the caller
 * show a genuine "scanning" LED state for exactly the window this call is
 * actually active, and lets it decide whether this is the very last
 * capture of the whole enrollment (which goes straight to a final success
 * indication instead of the routine per-capture one).
 *
 * @param[in] i_scan_num   1 for the first scan, 2 for the second.
 * @param[in] i_timeout_ms Time budget to retry the capture itself.
 *
 * @return RC_SUCCESS (scan captured) or RC_TIMEOUT.
 */
RC_t fps_EnrollStep(uint8_t i_scan_num, uint32_t i_timeout_ms);

/**
 * @brief Commit a completed 2-scan enrollment and tag it with metadata.
 *
 * Call once after two successful fps_EnrollStep() calls (scan_num 1, then
 * 2). Wraps fps_EnrollCommit() + fps_raw_write_meta() as one mutex-held
 * operation so a caller can never commit without tagging.
 *
 * @param[in]  meta  Metadata to store for the new slot.
 * @param[out] o_id  Assigned fingerprint slot ID.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t fps_EnrollCommitAndTag(const fpm_fingerprint_meta_t *meta, uint16_t *o_id);

/**
 * @brief Bounded-poll a 1:N identification scan.
 *
 * Same fail-fast-without-a-finger caveat as fps_EnrollStep(); retries
 * fps_Scan() every FPS_POLL_INTERVAL_MS until a definitive result (match
 * or no-match) or i_timeout_ms elapses.
 *
 * @param[in]  i_timeout_ms Overall time budget to wait for a finger.
 * @param[out] o_id         Matched fingerprint slot ID (valid on RC_SUCCESS).
 * @param[out] o_score      Match confidence score (valid on RC_SUCCESS).
 *
 * @return RC_SUCCESS (match), RC_NO_MATCH (scanned but no match), or
 *         RC_TIMEOUT (no finger presented within i_timeout_ms).
 */
RC_t fps_ScanStep(uint32_t i_timeout_ms, uint16_t *o_id, uint16_t *o_score);

/******************************************************************************/
/*** API Functions — Normal-Mode scan task                                    */
/******************************************************************************/

/**
 * @brief Normal-Mode fingerprint scan task (SWS-FPM201/202/203).
 *
 * Idles blocked on g_fp_sense_sem (the FP-sense GPIO interrupt's wake
 * signal). On wake, attempts a scan directly — no "awaiting finger" LED is
 * shown first, since the wake itself already means a finger just made
 * contact. Publishes a match (or no-match) event to g_scan_queue for
 * mqtt_Task. After showing the result for a fixed display time, polls
 * io_WaitFingerPresent() for a possible next scan in the same session;
 * once that times out, reverts to the idle baseline and blocks on the
 * semaphore again.
 *
 * Intended to run pinned to Core 0 at priority 5.
 *
 * @param pvParam  Unused; pass NULL when creating the task.
 */
void fps_ScanTask(void *pvParam);

#endif /* FPS_H_ */
