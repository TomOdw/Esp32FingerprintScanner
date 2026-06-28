/**
 * @file       ceh.h
 * @brief      Critical Event Handler
 *
 *             Handles fatal, unrecoverable errors. If an LED callback has been
 *             registered (after FPS init), shows a visual indication before
 *             rebooting. Otherwise reboots immediately.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef CEH_H_
#define CEH_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "basictypes.h"

/******************************************************************************/
/*** Types                                                                    */
/******************************************************************************/

/**
 * @brief Fatal error identifiers.
 *
 * Errors 1–2 occur before the sensor LED is available; reboot only.
 * Errors 3–4 occur after FPS init; the LED callback is invoked first.
 */
typedef enum
{
  CEH_ERR_FPM_INIT  = 1, /**< Sensor init or communication test failed  */
  CEH_ERR_NVS_INIT  = 2, /**< NVS flash init or namespace open failed   */
  CEH_ERR_RESOURCE  = 3, /**< FreeRTOS resource allocation failure (OOM) */
  CEH_ERR_WIFI_BOOT = 4, /**< WiFi connect timeout; AP fallback triggered */
} ceh_err_t;

/** Callback type for LED error indication. Registered via ceh_RegisterLed(). */
typedef void (*ceh_led_cb_t)(ceh_err_t i_err);

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Initialize the CEH.
 *
 * Must be the first call in app_main. Registers the IDF panic handler.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t ceh_Init(void);

/**
 * @brief Register the LED error callback.
 *
 * Called after fps_Init() succeeds. From this point on, ceh_Fatal() will
 * invoke @p i_cb before rebooting to give visual feedback.
 *
 * @param[in] i_cb  Non-NULL callback that maps a ceh_err_t to a sensor LED state.
 *
 * @return RC_SUCCESS or RC_INVALID_ARG.
 */
RC_t ceh_RegisterLed(ceh_led_cb_t i_cb);

/**
 * @brief Signal a fatal error and reboot.
 *
 * If a LED callback is registered, invokes it and blocks ~3 s for visibility,
 * then calls esp_restart(). Never returns.
 *
 * @param[in] i_err  Reason for the fatal error.
 */
void ceh_Fatal(ceh_err_t i_err) __attribute__((noreturn));

#endif /* CEH_H_ */
