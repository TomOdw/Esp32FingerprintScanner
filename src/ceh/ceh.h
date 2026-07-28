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
 * Errors 3–7 occur after FPS init; the LED callback is invoked first
 * (SWS-CEH005's blink-code mapping lives in app.c's ceh_led_callback()).
 */
typedef enum
{
  CEH_ERR_FPM_INIT     = 1, /**< Sensor init or communication test failed   */
  CEH_ERR_NVS_INIT     = 2, /**< NVS flash init or namespace open failed    */
  CEH_ERR_RESOURCE     = 3, /**< FreeRTOS resource allocation failure (OOM) */
  CEH_ERR_WIFI_BOOT    = 4, /**< WiFi connect timeout; AP fallback triggered */
  CEH_ERR_WIFI_RUNTIME = 5, /**< WiFi dropped during operation, reconnect timed out */
  CEH_ERR_MQTT_RUNTIME = 6, /**< MQTT broker unreachable at boot, or dropped during operation */
  CEH_ERR_WATCHDOG     = 7, /**< Cooperative software watchdog trip         */
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
 * @brief Signal a fatal error and reboot (SWS-CEH004).
 *
 * Always logs (ESP_LOGE) and records a deduplicated NVS error FIFO entry
 * (SWS-CEH002) before acting. If no LED callback is registered yet (very
 * early boot), reboots immediately. Otherwise repeats the callback's blink
 * code with a 2 s pause between repetitions for up to 30 s total, then
 * calls esp_restart(). Never returns.
 *
 * @param[in] i_err  Reason for the fatal error.
 */
void ceh_Fatal(ceh_err_t i_err) __attribute__((noreturn));

/**
 * @brief Signal a non-fatal, recoverable error (SWS-CEH003).
 *
 * Logs (ESP_LOGW) and records a deduplicated NVS error FIFO entry
 * (SWS-CEH002). No LED change, no reboot — used while a retry for the
 * condition is already in progress (e.g. WiFi/MQTT reconnect attempts).
 *
 * @param[in] i_err     Reason for the condition.
 * @param[in] i_detail  Optional short human-readable detail appended to the
 *                       NVS FIFO entry, or NULL for none.
 */
void ceh_NonFatal(ceh_err_t i_err, const char *i_detail);

/**
 * @brief Mark a specific error code's ongoing condition as resolved.
 *
 * Called by whichever module resolves its own condition (e.g. wifi.c on a
 * successful WiFi reconnect calls this with CEH_ERR_WIFI_RUNTIME; mqtt.c on
 * a successful MQTT reconnect calls this with CEH_ERR_MQTT_RUNTIME) so the
 * next ceh_NonFatal()/ceh_Fatal() call for that same error code is treated
 * as a fresh occurrence rather than being deduplicated against the
 * resolved one (SWS-CEH002). Each error code's dedup state is tracked
 * independently, so clearing one never affects another.
 *
 * @param[in] i_err  Which error code's condition has been resolved.
 */
void ceh_ClearCondition(ceh_err_t i_err);

#endif /* CEH_H_ */
