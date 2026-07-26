/**
 * @file       mode/mode.h
 * @brief      Mode-control: Normal-Mode vs Setup-Mode boot decision
 *
 *             Implements SWS-MOD001-003 (boot decision), SWS-MOD104
 *             (setup-enter flag consumption) and SWS-MOD108 (admin
 *             fingerprint presence, used to distinguish First-Run-Mode
 *             from Normal-Setup-Mode within the webpage module).
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-07-25
 *
 * @version    0.1  Initial Version
 */
#ifndef MODE_H_
#define MODE_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "basictypes.h"

/******************************************************************************/
/*** Types                                                                    */
/******************************************************************************/

/**
 * @brief Device operating mode, decided once at boot.
 *
 * Named app_mode_t (not mode_t) to avoid clashing with the toolchain's
 * sys/types.h mode_t.
 */
typedef enum
{
  APP_MODE_NORMAL, /**< Normal operation: STA wifi, scanner_task, mqtt_Task. */
  APP_MODE_SETUP,  /**< Setup: AP wifi, webpage interface, no scanning/mqtt. */
} app_mode_t;

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Decide Normal-Mode vs Setup-Mode (SWS-MOD002/003).
 *
 * Setup-Mode is entered when the setup-enter flag is set, or wifi SSID is
 * unconfigured, or the MQTT broker is unconfigured. Pure NVS reads — call
 * any time after nvs_Init() succeeds.
 *
 * @return APP_MODE_SETUP or APP_MODE_NORMAL.
 */
app_mode_t app_mode_Decide(void);

/**
 * @brief Consume the setup-enter flag on entry into Setup-Mode (SWS-MOD104).
 *
 * Call exactly once, only when app_mode_Decide() returned APP_MODE_SETUP,
 * before any other Setup-Mode work begins.
 */
void app_mode_EnterSetup(void);

/**
 * @brief Check whether an admin/master fingerprint is enrolled (SWS-MOD108).
 *
 * Backed by a persisted NVS flag (nvs_GeneralGetAdminEnrolled), set once at
 * the end of a successful First-Run-Mode enrollment — not a live sensor
 * scan. The flag and the sensor's fingerprint library are kept in sync by
 * construction: the flag is only ever set right after a verified
 * enrollment commit, and only ever cleared by the factory-reset flow that
 * also erases the sensor's fingerprint library (see webpage.c).
 *
 * @return true if an admin fingerprint is enrolled, false otherwise.
 */
bool app_mode_HasAdminFingerprint(void);

#endif /* MODE_H_ */
