/**
 * @file       webpage/webpage.h
 * @brief      Setup-Mode webpage interface (HTTP server + JSON API)
 *
 *             Implements the Webpage Interface specification (SWS-WP001-202):
 *             a single-page, mobile-first admin UI served over the Setup-Mode
 *             AP, backed by a small JSON API for System Settings, User
 *             Settings, and Fingerprint Library management, plus a
 *             First-Run-Mode admin-enrollment wizard.
 *
 *             Only usable in Setup-Mode: must be started after the wifi
 *             module has confirmed AP mode is up (EVT_WIFI_AP_MODE) and
 *             after fps_Init()/g_fpm_mutex are ready.
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-07-25
 *
 * @version    0.1  Initial Version
 */
#ifndef WEBPAGE_H_
#define WEBPAGE_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include "basictypes.h"

/******************************************************************************/
/*** Types                                                                    */
/******************************************************************************/

/**
 * @brief Which screen the single-page app opens into (SWS-MOD108).
 */
typedef enum
{
  WEBPAGE_MODE_FIRST_RUN,    /**< No admin fingerprint yet — enrollment wizard. */
  WEBPAGE_MODE_NORMAL_SETUP, /**< Admin exists — full setup menu.               */
} webpage_mode_t;

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Start the Setup-Mode HTTP server and register all URI handlers.
 *
 * @param[in] i_mode  Determines what GET /api/mode reports to the frontend.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t webpage_Init(webpage_mode_t i_mode);

/**
 * @brief Stop the HTTP server.
 *
 * Not used in the normal boot flow (Setup-Mode always ends in an
 * intentional reboot instead); provided for symmetry/testability.
 *
 * @return RC_SUCCESS or RC_ERROR.
 */
RC_t webpage_Deinit(void);

#endif /* WEBPAGE_H_ */
