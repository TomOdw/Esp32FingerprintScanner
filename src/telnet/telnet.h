/**
 * @file       telnet.h
 * @brief      Telnet server (single session)
 *
 *             Accepts one Telnet session at a time on port 23. While a session
 *             is active:
 *               - Received bytes are posted to g_cli_input_queue (shared with UART).
 *               - cli_task output is mirrored here via telnet_write().
 *             After the client disconnects, the server returns to accept().
 *
 * @author     Tom Christ
 * @copyright  Copyright (c) 2026 Tom Christ; MIT License
 * @date       2026-06-07
 *
 * @version    0.1  Initial Version
 */
#ifndef TELNET_H_
#define TELNET_H_

/******************************************************************************/
/*** Include files                                                            */
/******************************************************************************/
#include <stddef.h>
#include "basictypes.h"

/******************************************************************************/
/*** Defines                                                                  */
/******************************************************************************/

#define TELNET_PORT 23U

/******************************************************************************/
/*** API Functions                                                            */
/******************************************************************************/

/**
 * @brief Write bytes to the active Telnet session.
 *
 * Called by cli_Task's broadcast output handle. Safe to call when no session
 * is connected — the write is silently dropped.
 *
 * @param[in] i_buf  Data to send.
 * @param[in] i_len  Number of bytes.
 *
 * @return RC_SUCCESS or RC_ERROR (no session, send failure).
 */
RC_t telnet_Write(const char *i_buf, size_t i_len);

/**
 * @brief Telnet server task.
 *
 * Listens on TELNET_PORT, manages the session lifecycle, and feeds received
 * bytes to g_cli_input_queue. Intended to run on Core 0 at priority 3.
 *
 * @param pvParam  Unused; pass NULL when creating the task.
 */
void telnet_Task(void *pvParam);

#endif /* TELNET_H_ */
